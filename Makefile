CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pedantic
LDFLAGS ?=

TARGET := rummy-cli
SRC := src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Build examples with variant defaults:
# make VARIANT=GIN
# make VARIANT=INDIAN
VARIANT ?=
ifeq ($(VARIANT),GIN)
CXXFLAGS += -DRUMMY_VARIANT_GIN
endif
ifeq ($(VARIANT),INDIAN)
CXXFLAGS += -DRUMMY_VARIANT_INDIAN
endif
ifeq ($(VARIANT),R500)
CXXFLAGS += -DRUMMY_VARIANT_500
endif

.PHONY: all clean
