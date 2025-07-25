IOS := -Oz -fvisibility=hidden -fno-stack-protector -arch arm64 -mios-version-min=12.0 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS12.1.sdk

PHYSPUPPET_OBJS := $(patsubst %.m,%.o,$(wildcard physpuppet/*.c))

stages: stage1.o stage2.o hsp4.o trustcache.o kutils.o kpf.o $(PHYSPUPPET_OBJS)
	clang ${IOS} $^ -shared -o $@ -bind_at_load \
		-fno-stack-protector -fobjc-arc -fmodules -framework IOKit -lc++
	strip -u -r -s exports.txt stages

%.o: %.c
	clang -c ${IOS} $< -o $@ -fno-stack-protector -fmodules

%.o: %.cpp
	clang -c ${IOS} $< -o $@ -fno-stack-protector -fmodules

.PHONY: clean
clean:
	rm -f *.o *.dylib physpuppet/*.o
