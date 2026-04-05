CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.
TARGET   = software_cpu

SRCS = main.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

run-timer: $(TARGET)
	./$(TARGET) 1

run-hello: $(TARGET)
	./$(TARGET) 2

run-fibonacci: $(TARGET)
	./$(TARGET) 3

run-factorial: $(TARGET)
	./$(TARGET) 4

run-memory: $(TARGET)
	./$(TARGET) 5

.PHONY: all clean run run-timer run-hello run-fibonacci run-factorial run-memory
