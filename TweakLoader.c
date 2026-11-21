// TweakLoader - tolerant loader for Triumph 2.134
// Loads (in order): iGameGod framework -> CydiaSubstrate shim -> tweaks.
// Any single failure is logged, never fatal (matches old merged-build behavior).
// Compile:
//   xcrun -sdk iphoneos clang -x objective-c -shared -fobjc-arc \
//     -framework Foundation -framework UIKit -arch arm64 \
//     -miphoneos-version-min=13.0 -o TweakLoader.dylib TweakLoader.c

#import <objc/runtime.h>
#import <dlfcn.h>
#import <pthread.h>
#import <unistd.h>
#import <Foundation/Foundation.h>

extern void MSHookFlush(void);

static void *try_load(const char *label, const char *path) {
    void *h = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (h) { NSLog(@"[TweakLoader] OK   %s", label); }
    else   { NSLog(@"[TweakLoader] FAIL %s : %s", label, dlerror()); }
    return h;
}

static void load_all(void) {
    usleep(300000); // let app finish basic launch

    // 1. iGameGod cheat menu framework (embedded, randomized name)
    try_load("iGameGod framework", "@executable_path/Frameworks/w1z6p5en.framework/w1z6p5en");

    // 2. CydiaSubstrate shim (app bundle root)
    try_load("CydiaSubstrate shim", "@executable_path/CydiaSubstrate");

    // 3. Identity spoofer (patched to use @executable_path/CydiaSubstrate)
    try_load("MaxIdentity", "@executable_path/Frameworks/MaxIdentity.dylib");

    // 4. Cheat menu loader (pure ObjC, needs @rpath/libloader.dylib)
    try_load("iGameGodLoader", "@executable_path/Frameworks/iGameGodLoader.dylib");

    // 5. Decryption tweak (needs @rpath/libroot.dylib - may fail, tolerated)
    try_load("bfdecrypttweak", "@executable_path/Frameworks/bfdecrypttweak.dylib");

    // 6. iGSpoof (needs @rpath/libroot.dylib - may fail, tolerated)
    try_load("iGSpoof", "@executable_path/Frameworks/iGSpoof.dylib");

    // 7. Apply all queued function rebindings across every image
    MSHookFlush();
    NSLog(@"[TweakLoader] sequence complete");
}

__attribute__((constructor))
static void ctor(void) {
    pthread_t t;
    pthread_create(&t, NULL, (void *(*)(void *))load_all, NULL);
    pthread_detach(t);
}
