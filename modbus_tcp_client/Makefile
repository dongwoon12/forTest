
.SUFFIXES: .o .c
.c.o:
	$(CC) -c $< $(CFLAGS)


NAME	= modbus_tcp_client
TARGET 	= lib$(NAME).a
HEADER	= \
	$(NAME).h

SOURCE 	= \
	$(NAME).c

OBJECT	= $(SOURCE:.c=.o)

TOOL_TARGET = $(NAME)_test
TOOL_SOURCE = $(NAME)_test.c
TOOL_OBJECT = $(TOOL_SOURCE:.c=.o)

all: lib_bulid tool_build

lib_bulid:
	make $(TARGET)

tool_build :
	make $(TOOL_TARGET)


$(TARGET): $(OBJECT)
	$(AR) rcs $@ $^
	cp $@ ../bin
	cp $(HEADER) ../include

$(TOOL_TARGET): $(TOOL_OBJECT) ../bin/$(TARGET)
	$(CC) -o $@ $^ $(LFLAGS) -l$(NAME) 

clean:
	find . ../ ../bin -name '*.o' -o -name '*.d' -o -name '$(TARGET)' | xargs rm -f
	rm -f $(TARGET)
	rm -f ../include/$(HEADER)

%.d: %.c
	$(SHELL) -ec '$(CC) -M $(CFLAGS) $< | sed "s/$*.o/& $@/g" > $@'

include $(SOURCE:.c=.d)
