---
source_url: https://eprints.gla.ac.uk/381094/
ingested: 2026-10-14
sha256: b8d35a35dc3c67779ecceec15c1c98ab771e9f713c419e354f25c341234db643
fetched_via: exa-contents
---
# Evaluating and Enhancing Memory Safety in LLM-Generated Code on CHERI

Evaluating and Enhancing Memory Safety in LLM-Generated Code on CHERI 

# Evaluating and Enhancing Memory Safety in LLM-Generated Code on CHERI 

 Qin, Yuxin ORCID: https://orcid.org/0000-0003-1088-9950, Singer, Jeremy ORCID: https://orcid.org/0000-0001-9462-6802 and Wang, Xin (2026) Evaluating and Enhancing Memory Safety in LLM-Generated Code on CHERI. In: 29th International Conference on Computer Supported Cooperative Work in Design (CSCWD 2026), Fuzhou, China, 13 - 15 May 2026, ISBN 9798331584719 (doi: 10.1109/CSCWD68734.2026.11582098) 

| | Text 381094.pdf - Accepted Version Available under License Creative Commons Attribution. 307kB |
| --- | --- |

## Abstract

We investigate whether large language models (LLMs) can generate low-level C code that satisfies practical memory safety requirements under capability enforcement. By prompting multiple local LLMs to produce 120 compilable programs for binary parsing, record decoding, and image/log processing, and analyzing them with CBMC, AddressSanitizer (ASan), Valgrind, and the CHERI platform (Arm Morello), we find that memory safety violations are prevalent. Error patterns, particularly in pointer and raw buffer manipulations, closely resemble mistakes made by novice programmers.Notably, we find that CHERI not only detects all violations caught by traditional tools but also reliably traps spatial violations that remain latent on conventional systems, demonstrating its potential for securing untrusted LLM-generated code.Motivated by these findings, we propose a two-stage collaborative workflow model that combines LLM generation with hardware-assisted CHERI enforcement. As a first step, we implement a prototype integrating prompt-plus-RAG with CHERI and WARDuino, achieving a 3.2× speedup and 86% first-run success on memory safety benchmarks—demonstrating a promising path toward safer automated systems code.

| Item Type: | Conference Proceedings |
| --- | --- |
| Additional Information: | Financial support was provided by the National Natural Science Foundation of China (62373142) |
| Status: | Published |
| Refereed: | Yes |
| Glasgow Author(s) Enlighten ID: | Wang, Professor Xin and Qin, Yuxin and Singer, Professor Jeremy |
| Authors: | Qin, Y., Singer, J., and Wang, X. |
| College/School: | College of Science and Engineering > School of Computing Science |
| ISSN: | 2835-639X |
| ISSN (Online): | 2768-1904 |
| ISBN: | 9798331584719 |
| Copyright Holders: | Copyright © 2026 IEEE |
| First Published: | First published in 2026 29th International Conference on Computer Supported Cooperative Work in Design (CSCWD) |
| Publisher Policy: | Reproduced in accordance with the publisher copyright policy |
| Related URLs: | Organisation Publisher |

 Contact University of Glasgow Glasgow, G12 8QQ, Scotland Tel +44 (0) 141 330 2000 Contact Us
