NVCC := nvcc
TARGET := frodoKEM_gpu_test

SOURCES := \
	test_KEM976.cu \
	frodo976.cu \
	frodo_macrify_reference.cu \
	fips202.cu \
	random.cu \
	util.cu

NVCCFLAGS := -O3 -rdc=true
LIBS := -lcurand

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(NVCC) $(NVCCFLAGS) $(SOURCES) -o $@ $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
