PROG= mcastroute
SRCS= main.c
LDADD= -lc -lnetgraph
WARN?= 3
MAN=
.include <bsd.prog.mk>
