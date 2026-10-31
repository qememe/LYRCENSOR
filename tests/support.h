#ifndef LY_TEST_SUPPORT_H
#define LY_TEST_SUPPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#define T(x) do { if (!(x)) test_fail(__FILE__,__LINE__,#x); } while (0)
void test_fail(const char *file,int line,const char *expr);
void test_sleep(unsigned ms);
void write_all(int fd,const char *s,size_t n);
char *read_file(const char *path,size_t *len);
typedef struct { int status; const char *type,*body; unsigned delay_ms; bool truncate,dynamic; } Fixture;
typedef struct { pid_t pid; char url[128]; } Server;
Server server_start(const Fixture *responses,size_t count);
void server_join(Server s);
#endif
