LLVM_CONFIG = llvm-config
CLANG = clang
CXX = clang++
OPT = opt
LLVM_LINK = llvm-link

CXXFLAGS = `$(LLVM_CONFIG) --cxxflags` -fPIC -shared -O3 -g
LDFLAGS = `$(LLVM_CONFIG) --ldflags --system-libs --libs all` -g

PASS_NAME = llvm-jutsu
PASS_SO = $(PASS_NAME).so

all: example.obfuscated

# Pass needs lodepng linked in for compile-time PNG generation
$(PASS_SO): $(PASS_NAME).cpp lodepng.cpp lodepng.h
	$(CXX) $(CXXFLAGS) -o $@ $(PASS_NAME).cpp lodepng.cpp $(LDFLAGS)

# PNG helper bitcode (compiled separately, not obfuscated)
lodepng.bc: lodepng.cpp lodepng.h
	$(CXX) -O2 -emit-llvm -c -o $@ lodepng.cpp

hand_png_helper.bc: hand_png_helper.cpp lodepng.h
	$(CXX) -O2 -emit-llvm -c -o $@ hand_png_helper.cpp

# Compile example.c to IR
example.ll: example.c
	$(CLANG) -O0 -emit-llvm -S -o $@ $<

# Run obfuscation pass
obfuscated.bc: example.ll $(PASS_SO)
	$(OPT) -load-pass-plugin=./$(PASS_SO) -passes=llvm-jutsu -o $@ $<

# Link all IR together
linked.bc: obfuscated.bc lodepng.bc hand_png_helper.bc
	$(LLVM_LINK) $^ -o $@

# Whole-program optimization on linked IR
example.obfuscated.bc: linked.bc
	$(OPT) -O2 -o $@ $<

example.obfuscated.ll: example.obfuscated.bc
	llvm-dis $< -o $@

# Final binary
example.obfuscated: example.obfuscated.bc
	$(CLANG) -O2 $< -lc++ -o $@

clean:
	rm -f $(PASS_SO) *.o *.bc
	rm -f example.obfuscated.ll example.obfuscated
	rm -f example.ll

.PHONY: all clean
