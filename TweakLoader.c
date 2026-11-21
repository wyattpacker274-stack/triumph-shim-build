// TweakLoader - tolerant loader for Triumph 2.134 with explicit GameGod init
// Loads: iGameGod framework -> CydiaSubstrate shim -> tweaks -> calls GameGod.embedGameGod()
// Compile:
//   xcrun -sdk iphoneos clang -x objective-c -shared -fobjc-arc \
//     -framework Foundation -framework UIKit -arch arm64 \
//     -miphoneos-version-min=13.0 -o TweakLoader.dylib TweakLoader.c

#import <objc/runtime.h>
#import <dlfcn.h>
#import <pthread.h>
#import <unistd.h>
#import <Foundation/Foundation.h>

static void *try_load(const char *label, const char *path) {
    void *h = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (h) { NSLog(@"[TweakLoader] OK   %s", label); }
    else   { NSLog(@"[TweakLoader] FAIL %s : %s", label, dlerror()); }
    return h;
}

// GameGod class forward declaration
@class GameGod;

static void init_gamegod(void) {
    // Load the framework first
    void *fw = dlopen("@executable_path/Frameworks/w1z6p5en.framework/w1z6p5en", RTLD_LAZY | RTLD_GLOBAL);
    if (!fw) {
        NSLog(@"[TweakLoader] GameGod framework load failed: %s", dlerror());
        return;
    }
    NSLog(@"[TweakLoader] GameGod framework loaded");

    // Get GameGod class
    Class GameGodClass = objc_getClass("GameGod");
    if (!GameGodClass) {
        // Try via bundle
        NSBundle *bundle = [NSBundle bundleWithPath:@"/var/containers/Bundle/Application/../Frameworks/w1z6p5en.framework"];
        if (!bundle) {
            bundle = [NSBundle bundleWithPath:@"/var/containers/Bundle/Application/../Frameworks/w1z6p5en.framework/w1z6p5en"];
        }
        if (bundle) [bundle load];
        GameGodClass = objc_getClass("GameGod");
    }
    if (!GameGodClass) {
        NSLog(@"[TweakLoader] GameGod class not found after framework load");
        return;
    }
    NSLog(@"[TweakLoader] GameGod class found: %@", GameGodClass);

    // Call prepareResourceBundle
    SEL prepResSel = NSSelectorFromString(@"prepareResourceBundle");
    if ([GameGodClass respondsToSelector:prepResSel]) {
        [GameGodClass performSelector:prepResSel];
        NSLog(@"[TweakLoader] prepareResourceBundle called");
    }

    // Call prepareTemporaryStorage
    SEL prepTmpSel = NSSelectorFromString(@"prepareTemporaryStorage");
    if ([GameGodClass respondsToSelector:prepTmpSel]) {
        [GameGodClass performSelector:prepTmpSel];
        NSLog(@"[TweakLoader] prepareTemporaryStorage called");
    }

    // Call embedGameGod
    SEL embedSel = NSSelectorFromString(@"embedGameGod");
    if ([GameGodClass respondsToSelector:embedSel]) {
        [GameGodClass performSelector:embedSel];
        NSLog(@"[TweakLoader] embedGameGod called - CHEAT MENU SHOULD APPEAR");
    } else {
        NSLog(@"[TweakLoader] embedGameGod selector not found");
    }
}

static void load_all(void) {
    usleep(300000); // let app finish basic launch

    // 1. CydiaSubstrate shim (app bundle root) — MUST load first so symbols exist
    void *shim = try_load("CydiaSubstrate shim", "@executable_path/CydiaSubstrate");

    // 2. iGameGod cheat menu framework (embedded, randomized name)
    try_load("iGameGod framework", "@executable_path/Frameworks/w1z6p5en.framework/w1z6p5en");

    // 3. Identity spoofer (patched to use @executable_path/CydiaSubstrate)
    try_load("MaxIdentity", "@executable_path/Frameworks/MaxIdentity.dylib");

    // 4. Cheat menu loader (pure ObjC, needs @rpath/libloader.dylib)
    try_load("iGameGodLoader", "@executable_path/Frameworks/iGameGodLoader.dylib");

    // 5. Decryption tweak (needs @rpath/libroot.dylib - may fail, tolerated)
    try_load("bfdecrypttweak", "@executable_path/Frameworks/bfdecrypttweak.dylib");

    // 6. iGSpoof (needs @rpath/libroot.dylib - may fail, tolerated)
    try_load("iGSpoof", "@executable_path/Frameworks/iGSpoof.dylib");

    // 7. Resolve MSHookFlush from the shim we just loaded and call it
    if (shim) {
        void (*MSHookFlush)(void) = dlsym(shim, "MSHookFlush");
        if (MSHookFlush) {
            MSHookFlush();
            NSLog(@"[TweakLoader] MSHookFlush called");
        } else {
            NSLog(@"[TweakLoader] MSHookFlush not found in shim: %s", dlerror());
        }
    } else {
        NSLog(@"[TweakLoader] shim not loaded, skipping MSHookFlush");
    }

    // 8. Explicit GameGod initialization - this is what actually shows the menu
    init_gamegod();

    NSLog(@"[TweakLoader] sequence complete");
}

__attribute__((constructor))
static void ctor(void) {
    pthread_t t;
    pthread_create(&t, NULL, (void *(*)(void *))load_all, NULL);
    pthread_detach(t);
}
