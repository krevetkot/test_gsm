CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic -O2
TARGET  = csvutility
SRC     = main.c csvutility.c

TESTS = \
	1:t1.csv:0:basic_example_from_task \
	2:t2.csv:0:integer_arithmetic \
	3:t3.csv:0:negative_numbers \
	4:t4.csv:3:division_by_zero \
	5:t5.csv:3:circular_reference \
	6:t6.csv:3:bad_cell_reference \
	7:t7.csv:0:rows_not_in_order \
	8:t0.csv:1:missing_file \
	9:t9.csv:2:bad_header \
	10:t10.csv:2:duplicate_row_id \
	11:t11.csv:2:row_width_mismatch \
	12:t12.csv:3:invalid_string_cell

CHECK_CODE = \
	code=$$?; \
	if [ "$$code" -ne "$$expected" ]; then \
		echo "  FAIL: expected exit $$expected, got $$code"; \
		rm -f $$tmp; \
		exit 1; \
	fi;

CHECK_OUTPUT = \
	ref=./test/out/$$(echo $$file | sed 's/\.csv$$/.out/'); \
	if ! diff -q $$tmp $$ref > /dev/null 2>&1; then \
		echo "  FAIL: output mismatch"; \
		diff $$tmp $$ref; \
		rm -f $$tmp; \
		exit 1; \
	fi;

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)


test: $(TARGET)
	@tmp=$$(mktemp); \
	for t in $(TESTS); do \
		num=$$(echo $$t | cut -d: -f1); \
		file=$$(echo $$t | cut -d: -f2); \
		expected=$$(echo $$t | cut -d: -f3); \
		name=$$(echo $$t | cut -d: -f4- ); \
		echo "Test $$num: $$name"; \
		./$(TARGET) ./test/$$file > $$tmp 2>/dev/null; \
		$(CHECK_CODE) \
		if [ "$$expected" -eq 0 ]; then \
			$(CHECK_OUTPUT) \
		fi; \
		echo "	PASS"; \
		echo ""; \
	done; \
	rm -f $$tmp
	@echo "All tests done"