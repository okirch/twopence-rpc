
bindir	= /usr/bin
testdir	= /usr/lib/twopence/rpc

CCOPT	= -O2
CFLAGS	= -Wall $(CCOPT) -I/usr/include/tirpc -I.
APPS	= rpc.squared square rpctest getaddr \
	  bug940191
LINK	= -L. -lrpctest -lsuselog -ltirpc -lgssapi_krb5

SRVSRCS	= server_main.c
CLTSRCS	= client_main.c \
	  stress.c
TSTSRCS	= test_main.c
GADSRCS	= getaddr.c
LIBSRCS	= register.c \
	  netpath.c \
	  netconfig.c \
	  socket.c \
	  pmap.c \
	  rpcb.c \
	  svc.c \
	  clnt.c \
	  logging.c \
	  util.c \
	  square_impl.c \
	  square_svc.c \
	  square_clnt.c \
	  square_xdr.c
_RPCGEN	= square_clnt.c square_svc.c square_xdr.c
GENFILES= $(addprefix src/,$(_RPCGEN))
ALLSRCS	= $(SRVSRCS) $(CLTSRCS) $(TSTSRCS) $(GADSRCS) $(LIBSRCS)

LIB	= librpctest.a
SRVOBJS	= $(addprefix obj/,$(SRVSRCS:.c=.o))
CLTOBJS	= $(addprefix obj/,$(CLTSRCS:.c=.o))
LIBOBJS	= $(addprefix obj/,$(LIBSRCS:.c=.o))
TSTOBJS	= $(addprefix obj/,$(TSTSRCS:.c=.o))
GADOBJS	= $(addprefix obj/,$(GADSRCS:.c=.o))


all: $(APPS)

install: $(APPS)
	install -m 755 -d $(DESTDIR)$(bindir)
	install -m 555 $(APPS) $(DESTDIR)$(bindir)
	/usr/lib/susetest/twopence-install rpc twopence/nodes twopence/run $(DESTDIR)

tags: .FORCE
	ctags $(addprefix src/,$(ALLSRCS))

.FORCE: ;

clean:
	rm -rf obj $(APPS) $(LIB) $(GENFILES)

$(LIB): $(LIBOBJS)
	$(AR) cr $@ $(LIBOBJS)

rpc.squared: $(SRVOBJS) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(SRVOBJS) $(LINK)

square: $(CLTOBJS) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(CLTOBJS) $(LINK)

rpctest: $(TSTOBJS) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TSTOBJS) $(LINK)

getaddr: $(GADOBJS) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(GADOBJS) $(LINK)

bug940191: obj/bug940191.o $(LIB)
	$(CC) $(CFLAGS) -o $@ obj/bug940191.o $(LINK)


obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

src/square.h: src/square.x
	rm -f $@
	rpcgen -h -o $@ $<

src/square_xdr.c: src/square.x
	rm -f $@
	rpcgen -c -o $@ $<

src/square_clnt.c: src/square.x
	rm -f $@
	rpcgen -l -o $@ $<

src/square_svc.c: src/square.x
	rm -f $@
	rpcgen -m -o $@ $<

obj/square_xdr.o: src/square_xdr.c src/square.h
	$(CC) $(CFLAGS) -Wno-unused -c -o $@ $<

