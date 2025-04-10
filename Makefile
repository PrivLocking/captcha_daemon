ifeq ($(USER),root)
	$(info )
	$(info 838388381 you can NOT run by root !!! )
	$(info )
	$(error )
endif

.PHONY: all clean aaa a build

# your_secure_password_here1

source1 := src/captcha_daemon1.c
source2 := src/captcha_daemon2.c
source3 := src/captcha_daemon3.c

dst1    := bin/$(shell basename $(source1)).bin
dst2    := bin/$(shell basename $(source2)).bin
dst3    := bin/$(shell basename $(source3)).bin
CFLAGS := -Wall -O3 -static
CFLAGS := -Wall -Os -static
CFLAGS := -Wall -Os -ffunction-sections -fdata-sections -Wl,--gc-sections -static -D_FORTIFY_SOURCE=0 -DNDEBUG 
CFLAGS := -Wall -Oz -ffunction-sections -fdata-sections -Wl,--gc-sections -static -D_FORTIFY_SOURCE=0 -DNDEBUG 
CFLAGS := -Wall -O3 -ffunction-sections -fdata-sections -Wl,--gc-sections -static 
CFLAGS := -Wall -O3 -ffunction-sections -fdata-sections -Wl,--gc-sections -static
LDLIBS := -lgd -lpng -lz -ljpeg -lfreetype -lm -lmicrohttpd -lgnutls 
LDLIBS := -lgd -lpng -lz -ljpeg -lfreetype -lm 
LDLIBS := -lgd -lpng -lz -ljpeg -lfreetype -lm -lfontconfig -lbrotlidec -lbrotlicommon -lbz2 -lexpat 
LDLIBS := -lgd -lpng -lz -ljpeg -lfreetype -lm -lfontconfig -lbrotlidec -lbrotlicommon -lbz2 -lexpat  -lssl -lcrypto
#-lbrotlicommon -lbrotlienc 
installDir:=/home/nginX/bin/
installBin1:=/home/nginX/bin/$(shell basename $(dst1))
installBin2:=/home/nginX/bin/$(shell basename $(dst2))
installBin3:=/home/nginX/bin/$(shell basename $(dst3))


all:

b:b1
v:v1

m:
	vim Makefile
v1:
	vim $(source1)
v2:
	vim $(source2)
v3:
	vim $(source3)

aaa : 
	make b1 b2 b3
	make in

b1 : $(dst1)
	strip --strip-all --strip-unneeded $<
	md5sum $<
b2 : $(dst2)
	strip --strip-all --strip-unneeded $<
	md5sum $<
b3 : $(dst3)
	strip --strip-all --strip-unneeded $<
	md5sum $<

c clean:
	rm -rf bin/*.bin


in install:
	@echo
	-chmod   u+w   $(installDir)/
	cat   $(dst1)   > $(installBin1)
	cat   $(dst2)   > $(installBin2)
	cat   $(dst3)   > $(installBin3)
	@ls -l --color   $(installBin1) $(installBin2) $(installBin3) 
	@md5sum          $(installBin1) $(installBin2) $(installBin3) 
	@echo

vpc:
	rm -f tags \
		cscope.in.out \
		cscope.out \
		cscope.po.out
	rm -f _vim/file01.txt

vp vim_prepare : vpc
	mkdir -p _vim/
	echo $(Makefile)                                     > _vim/file01.txt
	-test -f Makefile.env && echo Makefile.env          >> _vim/file01.txt
	find -type f -name "*.c" \
		|grep -v '\.bak[0-9]*' \
		| xargs -n 1 realpath --relative-to=.|sort -u   >> _vim/file01.txt
	sed -i -e '/^\.$$/d' -e '/^$$/d'                       _vim/file01.txt
	cscope -q -R -b -i                                     _vim/file01.txt
	ctags -L                                               _vim/file01.txt
gs:
	git status
gd:
	git diff
gc:
	git add .
	git commit -a
up:
	git_ssh_example.sh git push
	sync



$(dst1): $(source1)
	$(CC) $(CFLAGS) -o $(dst1) $(source1) $(LDLIBS)
$(dst2): $(source2)
	$(CC) $(CFLAGS) -o $(dst2) $(source2) $(LDLIBS)
$(dst3): $(source3)
	$(CC) $(CFLAGS) -o $(dst3) $(source3) $(LDLIBS)

