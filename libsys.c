#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <io.h>

typedef struct tlistnode_s {
	struct tlistnode_s *prev, *next;
	void *data;
} TListNode;

typedef struct tlist_s {
	size_t node_size;
	TListNode *head, *tail;
} TList;

typedef bool(TLMatchFn)(void *data, void *args);

typedef struct timestamp_s {
	int16_t year;
	int8_t month;
	int8_t day;
	int8_t weekday;
	int8_t hour;
	int8_t min;
	int8_t sec;
} Timestamp;

enum UserGroup { User = 0, Manager, Admin };

enum Permission {
//! Service List
	BookService     = 0x0007,
	AccountService  = 0x0038,
	LibraryService  = 0x00C0,
	PropertyService = 0x0300,
	RecordService   = 0x0C00,
//! BookService
	Borrow          = 0b001,
	Return          = 0b010, 
	Query           = 0b100,
//! AccountService
	Register        = 0b001000,
	Login           = 0b010000,
	CancelAccount   = 0b100000,
//! LibraryService
	AddBook         = 0b01000000,
	ModifyBook      = 0b10000000,
//! PropertyService
	Recharge        = 0b0100000000,
	Deduct          = 0b1000000000,
//! RecordService
	NewRecord       = 0b010000000000,
	WithdrawRecord  = 0b100000000000,
//! UserGroup Access
	UserAccess      = BookService | AccountService | Recharge,
	ManagerAccess   = Query | LibraryService | RecordService,
	AdminAccess     = BookService | AccountService | LibraryService | PropertyService | RecordService,
};

typedef struct accountrecord_s {
	enum UserGroup group;  //@ 用户组
	char account[16];      //@ 账户
	char password[16];     //@ 密码
	uint32_t hashkey;      //@ 账户哈希
	uint32_t id;           //@ ID
	int32_t amount;        //@ 余额
	Timestamp tm_register; //@ 注册时间
} AccountRecord;

typedef struct bookrecord_s {
	size_t stock;           //@ 存量
	char ISBN[24];          //@ ISBN编号
	char author[32];        //@ 作者
	char name[64];          //@ 书名
	Timestamp tm_introduce; //@ 引入时间
} BookRecord;

typedef struct borrowrecord_s {
	char ISBN[24];        //@ ISBN编号
	uint32_t loan_time;   //@ 借阅天数
	uint32_t borrower_id; //@ 借阅人ID
	Timestamp tm_borrow;  //@ 借出时间
	Timestamp tm_return;  //@ 归还时间
} BorrowRecord;

typedef struct librarydbinfo_s {
	uint16_t account_rec_size;
	uint16_t book_rec_size;
	uint16_t borrow_rec_size;
	uint16_t unused_1;
	uint32_t account_rec_num;
	uint32_t book_rec_num;
	uint32_t borrow_rec_num;
	uint32_t unused_2;
} LibraryDBInfo;

typedef struct librarydb_s {
	LibraryDBInfo header;
	TList *AccountRecords;
	TList *BookRecords;
	TList *BorrowRecords;
} LibraryDB;

typedef struct session_s {
	AccountRecord *host_ref;
	Timestamp tm_establish;
} Session, *SessionID;

typedef struct bootinfo_s {
	char root[256];
} BootInfo;

typedef struct librarysystem_s {
	char *db_path;
	LibraryDB database;
	SessionID session;
} LibSysDescription, *LibrarySystem;

/// 辅助函数
uint32_t hash(const char *str) {
	register uint32_t hash_ = 5381;
	while (*str) {
		hash_ += (hash_ << 5) + *str++;
	}
	return hash_ & 0x7fffffff;
}

int getoption(const char *prompt) {
	char c;
	if (prompt != NULL) {
		printf(prompt);
	}
	while (isspace(c = getchar())) {}
	return c;
}

int getline(const char *prompt, char *buffer) {
	while (isblank(getchar())) {}
	if (prompt != NULL) {
		printf(prompt);
	}
	return scanf("%[^\n]", buffer);
}

void clear() {
	system("cls");
}

/// 通用链表支持
TList* MakeTList(size_t node_size) {
	assert(node_size >= 1);
	TList *list = (TList*)calloc(1, sizeof(TList));
	list->node_size = node_size;
	return list;
}

void TLDestroy(TList *list) {
	if (!list || !list->head) return;
	TListNode *p = list->head, *q = list->tail;
	while (p != q) {
		p = p->next;
		free(p->prev);
	}
	free(q);
	list->head = list->tail = NULL;
}

void* TLAlloc(TList *list) {
	assert(list != NULL);
	return calloc(1, list->node_size);
}

void* TLAppend(TList *list, void *data) {
	assert(list != NULL);
	TListNode *node = (TListNode*)calloc(1, sizeof(TListNode));
	void *node_data = TLAlloc(list);
	if (data != NULL) {
		memcpy(node_data, data, list->node_size);
	}
	node->data = node_data;
	if (list->head != NULL) {
		node->prev = list->tail;
		list->tail->next = node;
		list->tail = node;
	} else {
		list->head = list->tail = node;
	}
	return node->data;
}

bool TLErase(TList *list, TListNode *node) {
	if (!list || !list->head || !node) return false;
	if (node->prev) {
		node->prev->next = node->next;
	} else if (node != list->head) {
		return false;
	} else {
		list->head = list->head->next;
	}
	if (node->next) {
		node->next->prev = node->prev;
	} else if (node != list->tail) {
		return false;
	} else if (list->tail) {
		list->tail = list->tail->prev;
	}
	return true;
}

//! 完全匹配查找
void* TLFind(TList *list, void *data, bool retnode) {
	if (!list || !list->head) return NULL;
	TListNode *node = list->head;
	while (node != NULL) {
		if (memcmp(node->data, data, list->node_size) == 0) {
			return retnode ? node : node->data;
		}
		node = node->next;
	}
	return NULL;
}

//! 自定义搜索
void* TLMatch(TList *list, TLMatchFn match, void *args, bool retnode) {
	if (!list || !list->head) return NULL;
	TListNode *node = list->head;
	while (node != NULL) {
		if (match(node->data, args)) {
			return retnode ? node : node->data;
		}
		node = node->next;
	}
	return NULL;
}

/// 时间函数
void TimeToTimestamp(Timestamp *stamp, time_t tm) {
	struct tm *detail = localtime(&tm);
	stamp->year = detail->tm_year + 1900;
	stamp->month = detail->tm_mon + 1;
	stamp->day = detail->tm_mday;
	stamp->weekday = detail->tm_wday + 1;
	stamp->hour = detail->tm_hour;
	stamp->min = detail->tm_min;
	stamp->sec = detail->tm_sec;
}

void GetTimestamp(Timestamp *stamp) {
	assert(stamp != NULL);
	time_t rawtime;
	time(&rawtime);
	TimeToTimestamp(stamp, rawtime);
}

double GetDuration(Timestamp *begin, Timestamp *end) {
	assert(begin != NULL);
	assert(end != NULL);

	time_t tm_begin, tm_end;
	struct tm detail = { };
	detail.tm_isdst = -1;

	detail.tm_year = begin->year - 1900;
	detail.tm_mon = begin->month - 1;
	detail.tm_mday = begin->day;
	detail.tm_wday = begin->weekday - 1;
	detail.tm_hour = begin->hour;
	detail.tm_min = begin->min;
	detail.tm_sec = begin->sec;
	tm_begin = mktime(&detail);

	detail.tm_year = end->year - 1900;
	detail.tm_mon = end->month - 1;
	detail.tm_mday = end->day;
	detail.tm_wday = end->weekday - 1;
	detail.tm_hour = end->hour;
	detail.tm_min = end->min;
	detail.tm_sec = end->sec;
	tm_end = mktime(&detail);

	return difftime(tm_end, tm_begin);
}

/// 权限管理
bool RequireService(enum UserGroup identity, enum Permission service) {
	enum Permission access[] = {
		[User] UserAccess, [Manager] ManagerAccess, [Admin] AdminAccess
	};
	return !!(access[identity] & service);
}

bool CheckAccess(enum UserGroup identity, enum Permission op) {
	enum Permission access[] = {
		[User] UserAccess, [Manager] ManagerAccess, [Admin] AdminAccess
	};
	return (access[identity] & op) == op;
}

/// 数据管理
bool OpenLibraryDB(LibraryDB *db, const char *path) {
	if (!db) return false;
	if (access(path, F_OK) != 0) {
		FILE *fp = fopen(path, "wb+");
		if (fp == NULL) return false;
		memset(&db->header, 0, sizeof(LibraryDBInfo));
		db->header.account_rec_size = sizeof(AccountRecord);
		db->header.book_rec_size = sizeof(BookRecord);
		db->header.borrow_rec_size = sizeof(BorrowRecord);
		db->header.account_rec_num = 1;
		fwrite(&db->header, sizeof(LibraryDBInfo), 1, fp);
		db->AccountRecords = MakeTList(sizeof(AccountRecord));
		db->BookRecords = MakeTList(sizeof(BookRecord));
		db->BorrowRecords = MakeTList(sizeof(BorrowRecord));

		AccountRecord admin = { };
		admin.group = Admin;
		admin.id = 1;
		strcpy(admin.account, "admin");
		strcpy(admin.password, "admin");
		admin.amount = 0;
		admin.hashkey = hash(admin.account);
		GetTimestamp(&admin.tm_register);
		TLAppend(db->AccountRecords, &admin);

		fclose(fp);
	} else {
		FILE *fp = fopen(path, "rb");
		if (fp == NULL) return false;
		fread(&db->header, sizeof(LibraryDBInfo), 1, fp);
		db->AccountRecords = MakeTList(sizeof(AccountRecord));
		db->BookRecords = MakeTList(sizeof(BookRecord));
		db->BorrowRecords = MakeTList(sizeof(BorrowRecord));
		for (int n = 0, size = db->header.account_rec_size; n < db->header.account_rec_num; ++n) {
			AccountRecord record;
			fread(&record, size, 1, fp);
			TLAppend(db->AccountRecords, &record);
		}
		for (int n = 0, size = db->header.book_rec_size; n < db->header.book_rec_num; ++n) {
			BookRecord record;
			fread(&record, size, 1, fp);
			TLAppend(db->BookRecords, &record);
		}
		for (int n = 0, size = db->header.borrow_rec_size; n < db->header.borrow_rec_num; ++n) {
			BorrowRecord record;
			fread(&record, size, 1, fp);
			TLAppend(db->BorrowRecords, &record);
		}
		db->header.account_rec_size = sizeof(AccountRecord);
		db->header.book_rec_size = sizeof(BookRecord);
		db->header.borrow_rec_size = sizeof(BorrowRecord);
	}
	return true;
}

bool ExportLibraryDB(LibraryDB *db, const char *path) {
	if (!db) return false;
	FILE *fp = fopen(path, "wb+");
	if (fp == NULL) return false;
	fwrite(&db->header, sizeof(LibraryDBInfo), 1, fp);
	for (TListNode *p = db->AccountRecords->head; p != NULL; p = p->next) {
		fwrite(p->data, db->AccountRecords->node_size, 1, fp);
	}
	for (TListNode *p = db->BookRecords->head; p != NULL; p = p->next) {
		fwrite(p->data, db->BookRecords->node_size, 1, fp);
	}
	for (TListNode *p = db->BorrowRecords->head; p != NULL; p = p->next) {
		fwrite(p->data, db->BorrowRecords->node_size, 1, fp);
	}
	fclose(fp);
	return true;
}

void CloseLibraryDB(LibraryDB *db) {
	TLDestroy(db->AccountRecords);
	TLDestroy(db->BookRecords);
	TLDestroy(db->BorrowRecords);
	db->AccountRecords = NULL;
	db->BookRecords = NULL;
	db->BorrowRecords = NULL;
}

/// 会话管理与业务
bool AccountHashMatch(AccountRecord *record, AccountRecord *info) {
	if (record->hashkey != info->hashkey) return false;
	if (strcmp(record->account, info->account) != 0) return false;
	return true;
}

bool AccountIDMatch(AccountRecord *record, uint32_t *pid) {
	return record->id == *pid;
}

bool ISBNMatch(BookRecord *record, const char *ISBN) {
	return strcmp(record->ISBN, ISBN) == 0;
}

bool ExclusiveLogin(LibrarySystem sys, const char *account, const char *password) {
	AccountRecord info = { }, *user = NULL;
	strcpy(info.account, account);
	info.hashkey = hash(account);
	user = (AccountRecord*)TLMatch(sys->database.AccountRecords, (void*)AccountHashMatch, &info, false);
	if (!user) return false;
	if (strcmp(user->password, password) != 0) return false;
	sys->session = (SessionID)calloc(1, sizeof(Session));
	sys->session->host_ref = user;
	Timestamp tm;
	GetTimestamp(&sys->session->tm_establish);
	return true;
}

void RegisterAccount(LibrarySystem sys, const char *account, const char *password) {
	AccountRecord record;
	strcpy(record.account, account);
	strcpy(record.password, password);
	record.hashkey = hash(account);
	record.group = User;
	record.id = (uint32_t)(rand() * rand());
	record.amount = 0;
	GetTimestamp(&record.tm_register);
	TLAppend(sys->database.AccountRecords, &record);
	++sys->database.header.account_rec_num;
}

int GetBorrowNum(LibrarySystem sys) {
	if (sys->session == NULL) return 0;
	int id = sys->session->host_ref->id, count = 0;
	TListNode *p = sys->database.BorrowRecords->head;
	while (p != NULL) {
		BorrowRecord *record = (BorrowRecord*)p->data;
		if (record->borrower_id == id && record->tm_return.year == -1) {
			++count;
		}
		p = p->next;
	}
	return count;
}

/// 服务业务
//! 初始界面信息服务
void SvrInitial(LibrarySystem sys) {
	puts(
"================" "\n"
"    欢迎使用" "\n"
"  图书管理系统" "\n"
"================" "\n"
	);
}

//! 登录服务
void SvrLogin(LibrarySystem sys) {
	char opt = getoption(
"====选项====" "\n"
"[1] 登录"     "\n"
"[2] 注册"     "\n"
"[3] 返回"     "\n"
"============" "\n"
"$ ");

	switch (opt) {
		case '1': {
			int nfailed = 0;
			while (true) {
				char account[64], password[64];
				getline("账户：", account);
				getline("密码：", password);
				bool succeed = ExclusiveLogin(sys, account, password);
				if (succeed) {
					puts("登陆成功！");
					break;
				}
				puts("账户或密码错误！");
				if (++nfailed == 3) {
					getoption("多次登录失败，请尝试找回密码！[Y]");
					break;
				}
				while (!isspace(getchar())) {}
			}
		}
		break;
		case '2': {
			char account[64], password[64], confirm[64];
			int nfailed = 0;
			while (true) {
				getline("账户：", account);
				getline("密码：", password);
				getline("确认密码：", confirm);
				AccountRecord record;
				strcpy(record.account, account);
				strcpy(record.password, password);
				record.hashkey = hash(account);
				if (strcmp(password, confirm) != 0) {
					puts("两次密码不一致，请重试！");
				} else if (TLMatch(sys->database.AccountRecords, (void*)AccountHashMatch, &record, false)) {
					puts("账号已存在，请重试！");
				} else {
					RegisterAccount(sys, account, password);
					puts("注册成功！");
					break;
				}
				if (++nfailed == 3) {
					bool retry = true;
					while (retry) {
						retry = false;
						char opt = getoption("检测到多次注册失败，是否继续？[Y/n] ");
						while (!isspace(getchar())) {}
						if (tolower(opt) == 'y') {
							nfailed = 0;
						} else if (tolower(opt) != 'n') {
							retry = true;
						}
					}
					if (nfailed == 3) break;
					clear();
				}
			}
		}
		break;
		case '3': {
			clear();
			return;
		}
		break;
		default: {
			puts("未知选项！");
		}
	}
}

//! 个人信息预览服务
void SvrDatacard(LibrarySystem sys) {
	AccountRecord *user = sys->session->host_ref;
	puts("================");
	printf("ID：%d\n", user->id);
	printf("账户：%s\n", user->account);
	printf("密码：%s\n", user->password);
	printf("余额：%.2f元\n", user->amount * 0.01f);
	printf("借阅书目：%d本\n", GetBorrowNum(sys));
	printf("注册时间：%4d-%02d-%02d %02d:%02d:%02d\n",
		user->tm_register.year, user->tm_register.month, user->tm_register.day,
		user->tm_register.hour, user->tm_register.min, user->tm_register.sec);
	printf("上一次登录时间：%4d-%02d-%02d %02d:%02d:%02d\n",
		sys->session->tm_establish.year, sys->session->tm_establish.month, sys->session->tm_establish.day,
		sys->session->tm_establish.hour, sys->session->tm_establish.min, sys->session->tm_establish.sec);
	puts("================");
	getoption("按任意键继续[Y] ");
}

//! 账户注销服务
void SvrCancelAccount(LibrarySystem sys) {
	if (sys->session->host_ref->id == 1) {
		puts("无法删除内置管理员账户");
		return;
	}
	if (GetBorrowNum(sys) > 0) {
		puts("借阅书籍未全部归还，注销请求已拒绝！");
	} else if (sys->session->host_ref->amount < 0) {
		puts("当前账户滞还费未清缴，注销请求已拒绝！");
	} else {
		TListNode *node = TLFind(sys->database.AccountRecords, sys->session->host_ref, true);
		bool succeed = TLErase(sys->database.AccountRecords, node);
		if (succeed) {
			--sys->database.header.account_rec_num;
			free(sys->session);
			sys->session = NULL;
			puts("账户注销成功！");
		} else {
			puts("未知错误，账户注销失败！");
		}
	}
}

//! 充值服务
void SvrRecharge(LibrarySystem sys) {
	char buffer[64];
	getline("充值金额：", buffer);
	int amount = atoi(buffer);
	if (amount > 0) {
		sys->session->host_ref->amount += amount * 100;
	}
	puts(amount > 0 ? "充值成功！" : "无效充值金额！");
}

//! 账户管理服务
void SvrAccountManage(LibrarySystem sys) {
	if (sys->session->host_ref->group != Admin) {
		puts("账户管理服务未向当前用户开放！");
		return;
	}
	while (sys->session != NULL) {
		char opt = getoption(
"====操作====" "\n"
"[1] 用户列表" "\n"
"[2] 用户搜索" "\n"
"[3] 密码重置" "\n"
"[4] 注销用户" "\n"
"[5] 返回" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				TListNode *p = sys->database.AccountRecords->head;
				puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
				puts(" ID 账户 密码 余额 图书借阅数 ");
				while (p != NULL) {
					AccountRecord *backup = sys->session->host_ref;
					AccountRecord *record = (AccountRecord*)p->data;
					sys->session->host_ref = record;
					printf(" %d %s %s %.2f元 %d本\n",
						record->id, record->account, record->password,
						record->amount * 0.01f, GetBorrowNum(sys));
					sys->session->host_ref = backup;
					p = p->next;
				}
				puts("[______________________________]");
			}
			break;
			case '2': {
				char account[16];
				getline("用户名：", account);
				AccountRecord record = { };
				strcpy(record.account, account);
				record.hashkey = hash(account);
				AccountRecord *user = TLMatch(sys->database.AccountRecords, (void*)AccountHashMatch, &record, false);
				if (user == NULL) {
					puts("搜索结果不存在！");
				} else {
					printf("账户ID：%d\n", user->id);
				}
			}
			break;
			case '3': {
				char sid[16];
				getline("用户ID：", sid);
				uint32_t id = atoi(sid);
				TListNode *p = sys->database.AccountRecords->head;
				AccountRecord *target = NULL;
				while (p != NULL) {
					target = (AccountRecord*)p->data;
					if (target->id == id) {
						break;
					}
					target = NULL;
					p = p->next;
				}
				if (target == NULL) {
					puts("账户不存在！");
				} else if (target->id == 1) {
					puts("无法重置内置管理员账户的密码！");
				} else {
					strcpy(target->password, "123456");
					printf("ID为%d的用户密码已重置为\"123456\"\n", target->id);
				}
			}
			break;
			case '4': {
				char sid[16];
				getline("用户ID：", sid);
				uint32_t id = atoi(sid);
				TListNode *p = sys->database.AccountRecords->head;
				AccountRecord *target = NULL;
				while (p != NULL) {
					target = (AccountRecord*)p->data;
					if (target->id == id) {
						break;
					}
					target = NULL;
					p = p->next;
				}
				if (target == NULL) {
					puts("账户不存在！");
				} else if (target == sys->session->host_ref) {
					puts("无法删除当前账户！");
				} else {
					SessionID backup = (SessionID)calloc(1, sizeof(Session));
					memcpy(backup, sys->session, sizeof(Session));
					sys->session->host_ref = target;
					SvrCancelAccount(sys);
					if (sys->session == NULL) {
						sys->session = backup;
					} else {
						free(backup);
					}
				}
			}
			break;
			case '5': {
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 用户视图服务
void SvrAccountView(LibrarySystem sys) {
	while (sys->session != NULL) {
		char opt = getoption(
"====账户====" "\n"
"[1] 切换账号" "\n"
"[2] 个人信息" "\n"
"[3] 注销账号" "\n"
"[4] 充值" "\n"
"[5] 账户查询" "\n"
"[6] 返回" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				SvrLogin(sys);
				return;
			}
			break;
			case '2': {
				SvrDatacard(sys);
			}
			break;
			case '3': {
				SvrCancelAccount(sys);
				if (sys->session == NULL) return;
			}
			break;
			case '4': {
				SvrRecharge(sys);
			}
			break;
			case '5': {
				SvrAccountManage(sys);
			}
			break;
			case '6': {
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 书目浏览服务
void SvrBookList(LibrarySystem sys) {
	TListNode *p = sys->database.BookRecords->head;
	puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
	puts(" ISBN 书名 作者 存量 引入时间");
	while (p != NULL) {
		BookRecord *record = (BookRecord*)p->data;
		printf(" %s 《%s》 %s %d本 %4d-%02d-%02d\n",
			record->ISBN, record->name, record->author, record->stock,
			record->tm_introduce.year, record->tm_introduce.month, record->tm_introduce.day);
		p = p->next;
	}
	puts("[______________________________]");
}

//! 书目查询服务
void SvrSearchBook(LibrarySystem sys) {
	while (sys->session != NULL) {
		char opt = getoption(
"====检索====" "\n"
"[1] ISBN" "\n"
"[2] 书名（模糊检索）" "\n"
"[3] 作者（模糊检索）" "\n"
"[4] 返回" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				char ISBN[64];
				getline("ISBN编号：", ISBN);
				BookRecord *record = TLMatch(sys->database.BookRecords, (void*)ISBNMatch, ISBN, false);
				if (record == NULL) {
					puts("书籍不存在！");
				} else {
					printf("书名：《%s》 作者：%s 存量：%d本\n",
						record->name, record->author, record->stock);
				}
			}
			break;
			case '2': {
				char partial_name[64];
				getline("书名：", partial_name);
				puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
				TListNode *p = sys->database.BookRecords->head;
				while (p != NULL) {
					BookRecord *record = (BookRecord*)p->data;
					if (strstr(record->name, partial_name) != NULL) {
						printf(" ISBN：%s 书名：《%s》 作者：%s 存量：%d本\n",
							record->ISBN, record->name, record->author, record->stock);
					}
					p = p->next;
				}
				puts("[______________________________]");
			}
			break;
			case '3': {
				char partial_name[64];
				getline("作者：", partial_name);
				puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
				TListNode *p = sys->database.BookRecords->head;
				while (p != NULL) {
					BookRecord *record = (BookRecord*)p->data;
					if (strstr(record->author, partial_name) != NULL) {
						printf(" ISBN：%s 书名：《%s》 作者：%s 存量：%d本\n",
							record->ISBN, record->name, record->author, record->stock);
					}
					p = p->next;
				}
				puts("[______________________________]");
			}
			break;
			case '4': {
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 书目借阅服务
void SvrBorrow(LibrarySystem sys) {
	if (!CheckAccess(sys->session->host_ref->group, Borrow)) {
		puts("书籍借阅服务未向当前用户开放！");
		return;
	} else if (sys->session->host_ref->amount < 0) {
		puts("书籍借阅服务已向当前用户关闭，请缴清滞还费后再试！");
		return;
	}
	while (sys->session != NULL) {
		char ISBN[64], sday[64];
		int loan_time = 0;
		getline("ISBN编号：", ISBN);
		getline("借阅天数：", sday);
		BookRecord *book = TLMatch(sys->database.BookRecords, (void*)ISBNMatch, ISBN, false);
		if (book == NULL) {
			puts("借阅书籍不存在！");
		} else if (book->stock == 0) {
			puts("借阅书籍暂无存货！");
		} else if ((loan_time = atoi(sday)) <= 0) {
			puts("无效的借阅天数！");
		} else {
			BorrowRecord record = { };
			strcpy(record.ISBN, book->ISBN);
			record.loan_time = loan_time;
			record.borrower_id = sys->session->host_ref->id;
			GetTimestamp(&record.tm_borrow);
			record.tm_return.year = -1; // unreturned mark
			TLAppend(sys->database.BorrowRecords, &record);
			++sys->database.header.borrow_rec_num;
			--book->stock;
			puts("借阅成功！");
		}
		if (tolower(getoption("是否继续借阅？[Y/n] ")) != 'y') break;
	}
}

//! 书目新增服务
void SvrNewBook(LibrarySystem sys) {
	if (!RequireService(sys->session->host_ref->group, LibraryService)) {
		puts("图书管理服务未向当前用户开放！");
		return;
	}
	if (!CheckAccess(sys->session->host_ref->group, AddBook)) {
		puts("当前用户无权限添加书目！");
		return;
	}
	while (sys->session != NULL) {
		char ISBN[64], name[64], author[64], snumber[64];
		int number = 0;
		getline("ISBN编号：", ISBN);
		getline("书名：", name);
		getline("作者：", author);
		getline("数量：", snumber);

		BookRecord *book = TLMatch(sys->database.BookRecords, (void*)ISBNMatch, ISBN, false);
		if (book != NULL && (strcmp(book->name, name) != 0 || strcmp(book->author, author))) {
			puts("新增书目与已有书目信息冲突！已有书目信息如下：");
			printf("[ISBN：%s 书名：《%s》 作者：%s\n]\n");
		} else if ((number = atoi(snumber)) <= 0) {
			puts("新增书目数目应至少为一本！");
		} else if (book != NULL) {
			book->stock += number;
			puts("书籍数目已补充！");
		} else {
			BookRecord record = { };
			strcpy(record.ISBN, ISBN);
			strcpy(record.name, name);
			strcpy(record.author, author);
			record.stock = number;
			GetTimestamp(&record.tm_introduce);
			TLAppend(sys->database.BookRecords, &record);
			++sys->database.header.book_rec_num;
			puts("书目信息添加成功！");
		}

		if (tolower(getoption("是否继续添加？[Y/n] ")) != 'y') break;
	}
}

//! 书籍视图服务
void SvrBookView(LibrarySystem sys) {
	while (sys->session != NULL) {
		char opt = getoption(
"====操作====" "\n"
"[1] 书籍列表" "\n"
"[2] 书籍搜索" "\n"
"[3] 借阅书籍" "\n"
"[4] 新增书目" "\n"
"[5] 返回" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				SvrBookList(sys);
			}
			break;
			case '2': {
				SvrSearchBook(sys);
			}
			break;
			case '3': {
				SvrBorrow(sys);
			}
			break;
			case '4': {
				SvrNewBook(sys);
			}
			break;
			case '5': {
				clear();
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 个人借阅记录视图服务
void SvrUserBorrowView(LibrarySystem sys) {
	while (sys->session != NULL) {
		puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
		puts(" 索引 ISBN 书名 作者 借阅日期 借阅天数 ");
		TListNode *p = sys->database.BorrowRecords->head;
		int index = 0;
		while (p != NULL) {
			BorrowRecord *record = (BorrowRecord*)p->data;
			if (record->borrower_id == sys->session->host_ref->id
				&& record->tm_return.year == -1) {
				BookRecord *book = TLMatch(sys->database.BookRecords,
					(void*)ISBNMatch, record->ISBN, false);
				printf(" [%d] %s 《%s》 %s %4d-%02d-%02d %d\n",
					++index, book->ISBN, book->name, book->author,
					record->tm_borrow.year, record->tm_borrow.month, record->tm_borrow.day,
					record->loan_time);
			}
			p = p->next;
		}
		puts("[______________________________]");
		char opt = getoption(
"====操作====" "\n"
"[1] 归还" "\n"
"[2] 返回" "\n"
"============" "\n"
"$ ");
		switch (opt) {
			case '1': {
				char sindex[16];
				getline("待归还书目索引：", sindex);
				int return_id = atoi(sindex);
				if (return_id < 0 || return_id > index) {
					puts("索引书目不存在，请重试！");
				} else {
					BorrowRecord *target = NULL;
					TListNode *p = sys->database.BorrowRecords->head;
					int index = 0;
					while (p != NULL) {
						BorrowRecord *record = (BorrowRecord*)p->data;
						if (record->borrower_id == sys->session->host_ref->id
							&& record->tm_return.year == -1) {
							BookRecord *book = TLMatch(sys->database.BookRecords,
								(void*)ISBNMatch, record->ISBN, false);
							if (++index == return_id) {
								target = record;
								break;
							}
						}
						p = p->next;
					}
					AccountRecord *borrower = TLMatch(sys->database.AccountRecords,
						(void*)AccountIDMatch, &target->borrower_id, false);
					BookRecord *book = TLMatch(sys->database.BookRecords,
						(void*)ISBNMatch, target->ISBN, false);
					GetTimestamp(&target->tm_return);
					double diff = GetDuration(&target->tm_return, &target->tm_borrow);
					int days = (int)(diff / 86400);
					if (days > target->loan_time) {
						borrower->amount -= (days - target->loan_time) * 0.3 * 100; // ¥0.3/day
						printf("本次还书延迟%d天，共需支付%.2f元。\n", days - target->loan_time,
							(days - target->loan_time) * 0.3);
						if (borrower->amount < 0) {
							puts("您的余额不足，请及时充值并清缴滞还费！");
						}
					}
					++book->stock;
					puts("书籍归还成功！");
				}
			}
			break;
			case '2': {
				clear();
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 借阅记录视图服务
void SvrBorrowRecords(LibrarySystem sys) {
	clear();
	puts("[^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^]");
	puts(" ISBN 书名 作者 借阅人 借阅天数 借阅日期 归还日期 ");
	TListNode *p = sys->database.BorrowRecords->head;
	while (p != NULL) {
		BorrowRecord *record = (BorrowRecord*)p->data;
		AccountRecord *borrower = TLMatch(sys->database.AccountRecords,
			(void*)AccountIDMatch, &record->borrower_id, false);
		BookRecord *book = TLMatch(sys->database.BookRecords,
			(void*)ISBNMatch, record->ISBN, false);
		printf(" %s 《%s》 %s %s %d %4d-%02d-%02d ",
			book->ISBN, book->name, book->author, borrower->account, record->loan_time,
			record->tm_borrow.year, record->tm_borrow.month, record->tm_borrow.day);
		if (record->tm_return.year == -1) {
			printf("待还");
		} else {
			printf("%4d-%02d-%02d", record->tm_return.year,
				record->tm_return.month, record->tm_return.day);
		}
		putchar('\n');
		p = p->next;
	}
	puts("[______________________________]");
}

//! 借阅视图服务
void SvrBorrowView(LibrarySystem sys) {
	while (sys->session != NULL) {
		char opt = getoption(
"====操作====" "\n"
"[1] 借阅记录" "\n"
"[2] 返回" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				if (RequireService(sys->session->host_ref->group, RecordService)) {
					SvrBorrowRecords(sys);
				} else {
					SvrUserBorrowView(sys);
				}
			}
			break;
			case '2': {
				clear();
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 服务菜单服务
void SvrMenu(LibrarySystem sys) {
	while (sys->session != NULL) {
		char opt = getoption(
"====服务====" "\n"
"[1] 账户管理" "\n"
"[2] 搜索书目" "\n"
"[3] 借阅信息" "\n"
"[4] 退出" "\n"
"============" "\n"
"$ ");
		clear();
		switch (opt) {
			case '1': {
				SvrAccountView(sys);
			}
			break;
			case '2': {
				SvrBookView(sys);
			}
			break;
			case '3': {
				SvrBorrowView(sys);
			}
			break;
			case '4': {
				clear();
				return;
			}
			break;
			default: {
				puts("未知选项！");
			}
		}
	}
}

//! 引导服务
void SvrMain(LibrarySystem sys) {
	while (true) {
		while (sys->session == NULL) {
			puts("请先完成登录！");
			SvrLogin(sys);
			clear();
		}
		SvrMenu(sys);
		if (sys->session != NULL) break;
	}
}

/// 系统综合
LibrarySystem Boot(BootInfo *info) {
	char buf[256];
	snprintf(buf, 256, "%s\\librecords.db", info->root);
	LibrarySystem sys = (LibrarySystem)calloc(1, sizeof(LibSysDescription));
	if (!OpenLibraryDB(&sys->database, buf)) {
		free(sys);
		return NULL;
	}
	sys->db_path = strdup(buf);

	time_t tm;
	time(&tm);
	srand(tm);

	return sys;
}

void Shutdown(LibrarySystem *sys) {
	ExportLibraryDB(&(*sys)->database, (*sys)->db_path);
	CloseLibraryDB(&(*sys)->database);
	free((*sys)->db_path);
	free(*sys);
	*sys = NULL;
}

void Run(LibrarySystem sys) {
	SvrInitial(sys);
	SvrMain(sys);
	puts("服务已终止");
}

int main(int argc, char const *argv[])
{
	BootInfo info;
	getcwd(info.root, 256);
	LibrarySystem sys = Boot(&info);
	if (sys == NULL) {
		puts("开机失败！");
		return -1;
	}
	Run(sys);
	Shutdown(&sys);
	return 0;
}
