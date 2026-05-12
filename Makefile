CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic -O2
TARGET  = csvutility
SRC     = main.c

.PHONY: all clean test

all: $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

# ---------------------------------------------------------------
# Run all test cases
# ---------------------------------------------------------------
test: $(TARGET)
	@echo "Test 1: basic example from task"
	./$(TARGET) ./test/t1.csv
	@echo ""

	@echo "Test 2: integer arithmetic"
	./$(TARGET) ./test/t2.csv
	@echo ""

	@echo "Test 3: negative numbers"
	./$(TARGET) ./test/t3.csv
	@echo ""

	@echo "Test 4: division by zero (expect error)"
	./$(TARGET) ./test/t4.csv || echo "(extpect error)"
	@echo ""

	@echo "Test 5: circular reference (expect error)"
	./$(TARGET) ./test/t5.csv || echo "(expect error)"
	@echo ""

	@echo "Test 6: bad cell reference (expect error)"
	./$(TARGET) ./test/t6.csv || echo "(expect error)"
	@echo ""

	@echo "Test 7: rows not in order"
	./$(TARGET) ./test/t7.csv
	@echo ""

	@echo "All tests done"