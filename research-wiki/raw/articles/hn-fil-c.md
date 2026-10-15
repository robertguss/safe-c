---
source_url: https://news.ycombinator.com/item?id=45735877
ingested: 2026-10-14
sha256: 87483d06d291a1c1a545dc75377a3f358c49618fab3a3a3ae87fbea28024ac23
fetched_via: exa-contents
---
# Fil-C: A memory-safe C implementation | Hacker News

Fil-C: A memory-safe C implementation | Hacker News

Fil-C: A memory-safe C implementation (lwn.net)

283 points by chmaynard 11 months ago | hide | past | favorite | 135 comments

mbrock 11 months ago | next [–]

I'm working on packaging Fil-C for Nix, as well as integrating Fil-C as a toolchain in Nix so you can build any Nix package with Fil-C.

https://github.com/mbrock/filnix

It's working. It builds tmux, nethack, coreutils, Perl, Tcl, Lua, SQLite, and a bunch of other stuff.

Binary cache on https://filc.cachix.org so you don't have to wait 40 minutes for the Clang fork to build.

If you have Nix with flakes on a 64-bit Linux computer, you can run

```
    nix run github:mbrock/filnix#nethack

```

 right now!

Cloudef 11 months ago | parent | next [–]

Yeah, I was thinking nix will be probably one of the first things that can easily adapt Fil-C as it already packages in a way that allows different packages to be completely independent of each other, thus Fil-C's ABI compatibility does not matter. I assume other targets will be mostly enterprise distros where the perf hit and source compatibility issues are less of a concern, and memory safety is absolutely critical.

Fil-C compiled flatpaks might be a interesting target as well for normal desktop users. (e.g. running a browser)

I wonder if GPU graphics are possible in Fil-C land? Perhaps only if whole mesa stack is compiled using Fil-C as well, limiting GPU use to open drivers?

yjftsjthsd-h 11 months ago | parent | prev | next [–]

That is super cool, and I will probably start running it on at least a test box shortly.

How does python work? Of course I can just add filc.python to my system, but if I `python3 -m pip install whatever` will it just rebuild any C modules with the fil-c compiler?

mbrock 11 months ago | root | parent | next [–]

Right now Python simply does not work at all. I was trying last night just to get the compiler to build, but ran into some Fil-C panics at the last moment when it tries to "freeze" some modules? But it should work.

My idea is to move towards defining Fil-C as a "platform", meaning it would have its own ABI value, so it would look like a target called "x86_64-unknown-linux-filc", and then the magic of Nixpkgs is that it can instantiate a full realm called "pkgsCross.filc" which automatically has every package, whose packages are built on your ordinary non-FilC platform as a cross compilation.

When that works—I hope it works, I probably need help to get it working—you should be able to use all the Nixpkgs packaging helpers like

```
    pkgs.mkShell {
      packages = [
        (pkgsCross.filc.python3.withPackages (pypkgs: with pypkgs; [
          pandas
          requests
        ]))
      ];
    }

```

 This is already how Nixpkgs can have a pkgsCross.musl that builds everything with Musl as a cross compilation. So it seems reasonable and possible. It should be of wide interest for Nix users, since it would suddenly allow memory safety for whole swaths of the C/C++ packages, it would let NixOS use memory safe builds for core services like OpenSSHd, and so on...

I should probably try to gather some interest among people who are truly familiar with Nixpkgs cross compilation...

Cloudef 11 months ago | root | parent | next [–]

Yes filc as abi makes most sense. Do note that while fil-c seems to have pretty good source compatibility some stuff still require patches (like python!)

yjftsjthsd-h 11 months ago | root | parent | next [–]

I don't know how it's going on the nixpkgs side, but my impression from Alpine is that it's pretty normal to carry some patches for any ABI/platform that isn't GNU.

mbrock 11 months ago | root | parent | next [–]

Yep, you can look around in Nixpkgs and see a bunch of conditionally applied patches based on target platform.

My Fil-C flake already includes like 50 different patches extracted from the upstream port catalogue!

kragen 11 months ago | parent | prev | next [–]

That's very exciting! Thank you!

kragen 11 months ago | prev | next [–]

Either Fil-C or a different implementation of the same idea seems essential to me. A great deal of software has been written in C, and without some way of running it, we lose access to that intellectual heritage. But pervasive security vulnerabilities mean that the traditional "YOLO" approach to C compilation is a bad idea for software that has to handle untrusted input, such as Web browsing or email.

Pizlo seems to have found an astonishingly cheap way to do the necessary pointer checking, which hopefully I will be able to understand after more study. (The part I'm still confused about is how InvisiCaps work with memcpy.)

tialaramex points out that we shouldn't expect C programmers to be excited about Fil-C. The point tialaramex mentions is "DWIM", like, accessing random memory and executing in constant time, but I think most C programmers won't be willing to take a 4× performance hit. After all, if they wanted to be using a slow language, they wouldn't be writing their code in C. But I think that's the wrong place to look for interest: Fil-C's target audience is users of C programs, not authors of C programs. We want the benefits of security and continued usage of existing working codebases, without having to pay the cost to rewrite everything in Rust or TypeScript or whatever. And for many of us, much of the time, the performance hit may be acceptable.

nielsbot 11 months ago | parent | next [–]

I like to share this every time there's a post about memory safe C:

Apple has a memory-safer C compiler/variant they use to compile their boot loaders:

https://saaramar.github.io/iBoot_firebloom/

pizlonator 11 months ago | root | parent | next [–]

That was my idea and I wrote a good chunk of the compiler and runtime.

kragen 11 months ago | root | parent | next [–]

(For those who didn't make the connection, pizlonator also wrote Fil-C.)

geocar 11 months ago | root | parent | prev | next [–]

Why isn't the compiler frontend called fucc?

pizlonator 11 months ago | root | parent | next [–]

I was so tempted to call it that

RBO2 11 months ago | root | parent | next [–]

Why not call it secc ("sexy") ?

pizlonator 11 months ago | root | parent | next [–]

It's poor form to put the word "secure" (or any derivative of it) into a product that aims to provide more security.

Reason: there's no solution to the security problem. Fil-C doesn't solve security. Does it make things more secure? Yes! But there will always be more security issues. So imagine if I had called it "Secure C" (and then had a sexy compiler), and 10 years from now someone finds a comprehensive solution to the string injection problem in Secure C. What do they call their thing? Securer C? Secure C Pro? Secure Secure C?

kragen 11 months ago | root | parent | next [–]

I agree. I'd add that security is always relative to not just an existing level of security but to a threat model. There are threat models relative to which Fil-C doesn't make things more secure. (I can't think of one under which Fil-C makes things less secure, though.)

A similar criticism applies to "new". Newcastle is named after a castle built 945 years ago. Neuchâtel is named after a castle built 1014 years ago. Xavier (from Basque "etxeberri", "new house") is named after a castle built in the 10th century. Windows NT "new technology", etc.

jibal 11 months ago | root | parent | next [–]

https://en.wikipedia.org/wiki/Post-postmodernism

RBO2 11 months ago | root | parent | prev | next [–]

These are good points but no name is ever perfect. Just felt that secc was more memorable and would offer more levarage to this great project :-)

kragen 11 months ago | root | parent | next [–]

"Filthy" is already extremely memorable.

ctrust 11 months ago | root | parent | prev | next [–]

It isn't too late!

avadodin 11 months ago | root | parent | prev | next [–]

Forgive me for not putting in the effort this deserves, but the description of pointers in this thread reads a lot like the current description of Fil-C pointers from the main site.

What is the 64-bit-presenting representation of pointers in Fil-C?

That is, what does %p return and how does that work as a pointer?

pizlonator 11 months ago | root | parent | next [–]

https://fil-c.org/invisicaps

https://fil-c.org/invisicaps_by_example

avadodin 11 months ago | root | parent | next [–]

Re-reading tfm, it definitely had an answer to my dumb question(basically the same as any regular C pointer), but I still get the feeling that the explanations make jumps that may be obvious to you but absolutely not to me.

I sort of get a sense of what the flight pointers do after reading the linked page with the assembly listings( which on mobile looks like you're discussing hex dumps until one tries side scrolling, btw ) but my brain would expect something along the lines of: "this is the flight pointer and this is how it is managed for local variables, global variables, etc. This part of it is what your C program sees" at the beginning of the invisicaps article rather than "pointers are 64 bit like in regular C proceeds to talk about a tuple containing two pointers"(at least that's how it reads to me).

Probably just laziness on my part.

pizlonator 11 months ago | root | parent | next [–]

That’s really good feedback about how to better talk about this stuff. Thank you!

InvisiCaps are counterintuitive. Took me a while to come up with them. I still haven’t found a really clean way to explain them to folks

frumplestlatz 11 months ago | root | parent | prev | next [5 more]

[flagged]

actionfromafar 11 months ago | root | parent | next [–]

You don't want to - so you did it anyway?

pizlonator 11 months ago | root | parent | prev | next [–]

Sounds like you weren't there

frumplestlatz 11 months ago | root | parent | next [3 more]

jibal 11 months ago | root | parent | next [–]

That breaks HN rules.

kragen 11 months ago | root | parent | next [–]

(But revealing your identity would not.)

kragen 11 months ago | root | parent | prev | next [–]

Yeah, fat pointers are definitely a viable approach, but a lot of the existing C code that is the main argument for Fil-C assumes it can put a pointer in a long. (Most of the C code that assumed you could put it in an int has been flushed out by now, but that was a big problem when the Alpha came out.) I'm guessing that the amount of existing C code in Apple's bootloader is minimal, maybe 1000 lines, not the billions of lines you can compile with Fil-C.

matthewfcarlson 11 months ago | root | parent | next [–]

You’re off by a few orders of magnitude. I’ll grant you, what is the bootloader becomes a very complex question. Even if you scope it to just “what is the code physically etched into the chip as the mask ROM” (secureROM) you’re talking hundreds of thousands. If you’re talking about all the code that runs before the kernel starts executing you’re talking hundreds of millions.

kragen 11 months ago | root | parent | next [–]

No, I was only talking about the pre-existing C code that wasn't written for the bootloader, which therefore might have incompatibilities with fat pointers you had to hunt down and fix.

Also I'm really skeptical about your "hundreds of millions" number, even if we're talking about all the code that runs before the kernel starts. How do you figure? The entire Linux kernel doesn't contain a hundred millions of lines of code, and that includes all the drivers for network cards, SCSI controllers, and multiport serial boards that nobody's made in 30 years, plus ports to Alpha, HP PA-RISC, Loongson, Motorola 68000, and another half-dozen architectures. All of that contains maybe 30 million lines. glibc is half a million. Firefox 140.4.0esr is 33 million. You're saying that the bootloader is six times the size of Firefox?

Are you really suggesting that tens of gigabytes of source code are compiled into the bootloader? That would make the bootloader at least a few hu
