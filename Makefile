PROG= mcastroute
SRCS= main.c
CFLAGS+= -ggdb
LDADD= -lc -lnetgraph
WARN?= 3
MAN=
.include <bsd.prog.mk>
