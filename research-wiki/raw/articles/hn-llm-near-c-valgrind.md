---
source_url: https://news.ycombinator.com/item?id=44915397
ingested: 2026-10-14
sha256: f7c192101206d059902efc8664b73eede2162397b83a19a9bfc56fff707b9fbb
fetched_via: exa-contents
---
# I would never ever let an LLM anywhere near C code. If you need help from LLM to... | Hacker News

I would never ever let an LLM anywhere near C code. If you need help from LLM to... | Hacker News

drumnerd on Aug 15, 2025 | parent | context | favorite | on: I let LLMs write an Elixir NIF in C; it mostly wor...

I would never ever let an LLM anywhere near C code. If you need help from LLM to write a NIF that performs basic C calls to the OS, you probably can’t check if it’s safe. I mean, it needs at least to pass valgrind.

simonw on Aug 15, 2025 | next [–]

You can use something like Claude Code or Codex CLI and tell it to run valgrind as part of iterating on the code.

faangguyindia on Aug 16, 2025 | parent | next [–]

how does it work? does it shell out to subagent? or it has full access to cli? what makes it not perform rm -rf /

anuramat on Aug 16, 2025 | root | parent | next [–]

> full access to cli?

yes

> what makes it not perform rm -rf /

by default -- regexes and prayers, but you're expected to use sandboxing

winrid on Aug 16, 2025 | root | parent | prev | next [–]

It works really well IMO.

overbring_labs on Aug 15, 2025 | prev | next [–]

I mean, you aren't wrong. I'm looking into converting it into Rust with Rustler right now.

abrookewood on Aug 16, 2025 | parent | next [–]

Yep, was wondering why you didn't go down that path in the first place. Seems way safer.

leansensei on Aug 16, 2025 | root | parent | next [–]

It's because the most familiar thing to me that uses a NIF was Exqlite, so that was my starting point.

Using Rust and Rustler turned out to be way easier and it also now works across Elixir versions 1.14 to 1.18.

abrookewood on Aug 16, 2025 | root | parent | next [–]

Great outcome :)

overbring_labs on Aug 16, 2025 | root | parent | next [–]

Truly, way better than before: https://github.com/waseigo/disk_space

And it now runs on everything, across more Elixir and Erlang/OTP versions than before!

true_religion on Aug 15, 2025 | prev [–]

Security is a spectrum. If you totally control the input going into a program, it can be safe even if you didn't test it for memory leaks. The only errors that occur will be truly erroneous, not malicious and for many solutions that's fine.

At the very least, it's fine for personal projects which is something I'm getting into more and more: remembering that computers were meant to create convenience, so writing small programs to make life easier.

Alive-in-2025 on Aug 15, 2025 | parent | next [–]

For personal projects, ok security is different. But get out of that, and I'd do it even for that, you need defense in depth. You think you sanitized your input but your C program has a bug and a vulnerability - or your Java program or whatever has bugs. Almost everything has some bugs, and thus your vulnerabilities will hit eventually in your C program, even if you were careful.

I'd say absent some temporary hack to do something, my bad experiences won't let me say something is low risk. I worked at Microsoft years ago, and after the zillions of vulnerabilities were attacked by people around the time of windows 95 and computers on the net, we did serious code reviews in my team of the data access libraries. There were vast numbers of vulnerabilities. A group of 3 or 4 of us would sit in a room for 3 hours a day, one person a scribe, and we'd go over this c code that was ancient even then - we found problems everywhere, it was exhausting and shocking. The entire data access infrastructure was riddled with memory leaks, strings that were not length limited, input parameters that were not checked or sanitized, etc. I'm sure it was endemic across all components, not just there. We fixed some things, but we found so much shit.

Thank got I wasn't on the team trying to figure out what to do about those problems. I think they end of lifed a lot of stuff.

Muromec on Aug 16, 2025 | root | parent | next [–]

>The entire data access infrastructure was riddled with memory leaks, strings that were not length limited, input parameters that were not checked or sanitized, etc. I'm sure it was endemic across all components, not just there. We fixed some things, but we found so much shit.

Sounds like the original vibe coding.

SOLAR_FIELDS on Aug 15, 2025 | root | parent | prev | next [–]

I mean, what I hear from that is that an LLM who you tell to write as safe code as possible is probably going to do a better job than your average human engineer at it, and you still have to do the same verification work either way. So why not have the LLM write the code and you instead just spend time verifying it? In other words, if I give an LLM and an average C developer the same task who will perform better? Even if the average C developer does better but takes N hours to write it and I still have to spend M cycles reviewing the average C developer's work, I'd rather have N be written by a machine since I have to pay M anyway regardless of whether it came from a machine or human.

Ygg2 on Aug 15, 2025 | parent | prev | next [–]

> Security is a spectrum.

It's less spectrum and more that it's relative. Depends on attacker and what they seek to gain.

An unsecured server is an unsecured server. But there is a world of difference if they are attacked by CIA or local script kiddies.

rs186 on Aug 15, 2025 | parent | prev [–]

Outside personal projects, my take is that security really just comes in two flavors: CVE vs no CVE. I pick the former.
