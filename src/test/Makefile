
SOURCE_DIR=.
OBJECTS = histogram_test.o

EXES = $(patsubst %.o,$(TARGET_DIR)/%,$(OBJECTS))


.PHONY: test
test: $(EXES)

$(TARGET_DIR)/%: $(OBJECT_DIR)/%.o
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)

.SECONDARY:
$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ -c $^ $(INCLUDES)



