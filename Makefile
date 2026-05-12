CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic -O2
TARGET  = csvutility
SRC     = main.c csvutility.c

# CSV_OK=0  CSV_ERR_IO=1  CSV_ERR_FORMAT=2  CSV_ERR_EVAL=3
CHECK = \
	code=$$?; \
	if [ "$$code" -eq "$$expected" ]; then \
		echo "	PASS: exit $$code"; \
	else \
		echo "  FAIL: expected exit $$expected, got $$code"; \
		exit 1; \
	fi


.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Test 1: basic example from task"
	@expected=0; ./$(TARGET) ./test/t1.csv; $(CHECK)
	@echo ""

	@echo "Test 2: integer arithmetic"
	@expected=0; ./$(TARGET) ./test/t2.csv; $(CHECK)
	@echo ""

	@echo "Test 3: negative numbers"
	@expected=0; ./$(TARGET) ./test/t3.csv; $(CHECK)
	@echo ""

	@echo "Test 4: division by zero"
	@expected=3; ./$(TARGET) ./test/t4.csv; $(CHECK)
	@echo ""

	@echo "Test 5: circular reference"
	@expected=3; ./$(TARGET) ./test/t5.csv; $(CHECK)
	@echo ""
 
	@echo "Test 6: bad cell reference"
	@expected=3; ./$(TARGET) ./test/t6.csv; $(CHECK)
	@echo ""
 
	@echo "Test 7: rows not in order"
	@expected=0; ./$(TARGET) ./test/t7.csv; $(CHECK)
	@echo ""
 
	@echo "Test 8: missing file"
	@expected=1; ./$(TARGET) ./test/no_such_file.csv; $(CHECK)
	@echo ""
 
	@echo "Test 9: bad header"
	@expected=2; ./$(TARGET) ./test/t9.csv; $(CHECK)
	@echo ""
 
	@echo "Test 10: duplicate row id"
	@expected=2; ./$(TARGET) ./test/t10.csv; $(CHECK)
	@echo ""
 
	@echo "Test 11: row width mismatch"
	@expected=2; ./$(TARGET) ./test/t11.csv; $(CHECK)
	@echo ""


	@echo "All tests done"