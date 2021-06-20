   211: int find(const std::vector<byte>& data, const std::vector<byte>& search) {
   212:     const byte* dataBegin = &data[0];                       // r11
   213:     const byte* searchBegin = &search[0];                   // rbx
   214:     const byte* dataEnd = (dataBegin + data.size());        // rdi
   215:     const byte* searchEnd = (searchBegin + search.size());  // []
   216:     size_t maxI = data.size();                              // []
   217:     size_t maxJ = search.size();                            // r10
   218:
   219:     for (auto dataIter = dataBegin; dataIter < dataEnd; dataIter++) {
07FF6CE207BB8  cmp         r11,rdi // if (dataBegin >= dataEnd) return -1
07FF6CE207BBB  mov         rdx,r11 // rdx = dataBegin (dataIter => rdx)
07FF6CE207BBE  jae         find+79h (07FF6CE207BF5) // return -1
   220:         bool match = true;
   221:         for (size_t j=0; j<maxJ; j++) {
07FF6CE207BC0  test        r10,r10 // if (maxJ == 0)
07FF6CE207BC3  je          find+6Ah (07FF6CE207BE6) // skip loop -- return delta
07FF6CE207BC5  mov         r9,rbx // j = searchBegin (j => r9)
07FF6CE207BC8  mov         r8,rbx // r8 = searchBegin ()
07FF6CE207BCB  neg         r9 // 2s complement negation (?!)
   222:             if (*(dataIter + j) == *(searchBegin + j)) {
07FF6CE207BCE  mov         al,byte ptr [r8] // One of the derefs... I think this is the second one?
07FF6CE207BD1  lea         rcx,[r9+r8] // rcx = r8 - r9
07FF6CE207BD5  cmp         byte ptr [rcx+rdx],al
07FF6CE207BD8  jne         find+71h (07FF6CE207BED)
   220:         bool match = true;
   221:         for (size_t j=0; j<maxJ; j++) {
07FF6CE207BDA  inc         r8
07FF6CE207BDD  lea         rax,[r9+r8]
07FF6CE207BE1  cmp         rax,r10
07FF6CE207BE4  jb          find+52h (07FF6CE207BCE)
   223:                 continue;
   224:             }
   225:             match = false;
   226:             break;
   227:         }
   228:         if (match) return (int)(dataIter - dataBegin);
07FF6CE207BE6  sub         edx,r11d // dataIter = (int)dataIter - (int)dataBegin
07FF6CE207BE9  mov         eax,edx
07FF6CE207BEB  jmp         find+7Ch (07FF6CE207BF8) // return dataIter
    // This is the standard for loop nonsense. End of loop replays the starting condition.
   218:
   219:     for (auto dataIter = dataBegin; dataIter < dataEnd; dataIter++) {
07FF6CE207BED  inc         rdx
07FF6CE207BF0  cmp         rdx,rdi
07FF6CE207BF3  jmp         find+42h (07FF6CE207BBE)
   229:     }
   230:     return -1;
07FF6CE207BF5  or          eax,0FFFFFFFFh
07FF6CE207C02  ret
   231: }




// attempt 2 with more pointers. Pointer math is faster and harder for the compiler to mess up


   210:
   211: int find(const std::vector<byte>& data, const std::vector<byte>& search) {
   212:     const byte* dataBegin = &data[0]; // "Pointer to const bytes",   // rbx
   213:     const byte* searchBegin = &search[0];                            // r10
   214:     const byte* dataEnd = (dataBegin + data.size() - search.size()); // r11
   215:     const byte* searchEnd = (searchBegin + search.size());           // r9
   216:
   217:     for (auto dataIter = dataBegin; dataIter < dataEnd; dataIter++) {
07FF6415A7BBB  mov         rcx,rbx // dataIter = dataBegin (dataIter => rcx)
07FF6415A7BBE  cmp         rbx,r11 // if (dataIter >= dataEnd)
07FF6415A7BC5  jae         find+76h (07FF6415A7BF2) // return -1
   218:         bool match = true;
   219:
   220:         auto compareIter = dataIter;
07FF6415A7BC7  mov         r8,rcx // compareIter = dataIter (compareIter => r8)
   221:         for (auto searchIter = searchBegin; searchIter < searchEnd; searchIter++ && compareIter++) {
07FF6415A7BCA  mov         rdx,r10 // searchIter = searchBegin (searchIter => rdx)
// skip entire loop if search is trivial (which it never will be, sigh. Asserted.)
07FF6415A7BCD  cmp         r10,r9 // if (searchBegin >= searchEnd)
07FF6415A7BD0  jae         find+70h (07FF6415A7BEC) // return (dataIter - dataBegin)
   222:             if (*compareIter == *searchIter) continue;
07FF6415A7BD2  mov         al,byte ptr [rdx] // al = *searchIter
07FF6415A7BD4  cmp         byte ptr [r8],al // if (*compareIter != *searchIter)
07FF6415A7BD7  jne         find+68h (07FF6415A7BE4) // continue outer loop
// continue for loop
07FF6415A7BD9  inc         rdx // searchIter++
07FF6415A7BDC  inc         r8 // compareIter++
// check for success
07FF6415A7BDF  cmp         rdx,r9 // if (searchIter >= searchEnd)
07FF6415A7BE2  jmp         find+54h (07FF6415A7BD0) // return (dataIter - dataBegin)
// else continue inner loop, I guess? I don't see where the goto lives really. Maybe I removed it :/
07FF6415A7BE4  inc         rcx // dataIter++
07FF6415A7BE7  cmp         rcx,r11 // if (dataIter == dataEnd)
07FF6415A7BEA  jmp         find+49h (07FF6415A7BC5) // return -1
   223:             match = false;
   224:             break;
   225:         }
   233:         if (match) return (int)(dataIter - dataBegin);
07FF6415A7BEC  sub         ecx,ebx
07FF6415A7BEE  mov         eax,ecx
07FF6415A7BF0  jmp         find+79h (07FF6415A7BF5)
   234:     }
   235:     return -1;
07FF6415A7BF2  or          eax,0FFFFFFFFh
   236: }
07FF6415A7BF5  add         rsp,20h
07FF6415A7BF9  pop         rbx
07FF6415A7BFA  ret


// Attempt 3, using do/while to avoid some (always false) if checks.


   216:     const byte* dataBegin = &data[0];                                // rbx
   217:     const byte* searchBegin = &search[0];                            // r10
   218:     const byte* dataEnd = (dataBegin + data.size() - search.size()); // r11
   219:     const byte* searchEnd = (searchBegin + search.size());           // r9
07FF779477BC2  mov         rdx,rbx // dataIter = dataBegin (dataIter => rdx)
07FF779477BC9  mov         r8,r10 // searchIter = searchBegin
07FF779477BCC  cmp         byte ptr [rdx],dil // if (*dataIter != dil) goto nextDataPosition
07FF779477BCF  jne         $nextDataPosition (07FF779477BE8)
// inner loop
07FF779477BD1  inc         r8 // searchIter++
07FF779477BD4  cmp         r8,r9 // if (searchIter >= searchEnd)
07FF779477BD7  jae         $nextDataPosition+16h (07FF779477BFE) // return (int)(dataIter - dataBegin)
07FF779477BD9  mov         al,byte ptr [r8] // al = *searchIter
07FF779477BDC  mov         rcx,rdx // rcx = dataIter
07FF779477BDF  sub         rcx,r10 // rcx = dataIter - searchBegin
07FF779477BE2  cmp         byte ptr [rcx+r8],al // if *(dataIter - searchBegin + searchIter) == *searchIter
07FF779477BE6  je          find+55h (07FF779477BD1) // goto inner loop

07FF779477BE8  inc         rdx // rdx += 1
07FF779477BEB  cmp         rdx,r11 // if (dataIter < dataEnd)
07FF779477BEE  jb          find+4Dh (07FF779477BC9) // goto start of search
07FF779477BF0  or          eax,0FFFFFFFFh // eax = -1
07FF779477BF3  ret // return eax
07FF779477BFE  sub         edx,ebx // dataIter = (int)dataIter - (int)dataBegin
07FF779477C00  mov         eax,edx // eax = dataIter
07FF779477C02  jmp         $nextDataPosition+0Bh (07FF779477BF3) // return eax

   210:
   211: int find(const std::vector<byte>& data, const std::vector<byte>& search) {
   216:     const byte* dataBegin = &data[0];                                // rbx
   217:     const byte* searchBegin = &search[0];                            // r10
   218:     const byte* dataEnd = (dataBegin + data.size() - search.size()); // r11
   219:     const byte* searchEnd = (searchBegin + search.size());           // r9
   220:
   221:     auto dataIter = dataBegin;
07FF779477BC2  mov         rdx,rbx // dataIter = dataBegin (dataIter => rdx)
   222:     do {
   223:         auto compareIter = dataIter;
   224:         auto searchIter = searchBegin;
07FF779477BC9  mov         r8,r10 // searchIter = searchBegin
   225:         do {
   226:             if (*compareIter != *searchIter) goto nextDataPosition;
07FF779477BCC  cmp         byte ptr [rdx],dil // if (*dataIter != dil) goto nextDataPosition
07FF779477BCF  jne         $nextDataPosition (07FF779477BE8)
   227:             searchIter++;
07FF779477BD1  inc         r8 // searchIter++
   228:             compareIter++;
   229:         } while (searchIter < searchEnd);
07FF779477BD4  cmp         r8,r9 // if (searchIter >= searchEnd)
07FF779477BD7  jae         $nextDataPosition+16h (07FF779477BFE) // return (int)(dataIter - dataBegin)
07FF779477BD9  mov         al,byte ptr [r8] // al = *searchIter
07FF779477BDC  mov         rcx,rdx
07FF779477BDF  sub         rcx,r10
07FF779477BE2  cmp         byte ptr [rcx+r8],al
07FF779477BE6  je          find+55h (07FF779477BD1)
   232:
   233:         nextDataPosition: dataIter++;
07FF779477BE8  inc         rdx
   234:     } while (dataIter < dataEnd);
07FF779477BEB  cmp         rdx,r11
07FF779477BEE  jb          find+4Dh (07FF779477BC9)
   235:
   236:     return -1;
07FF779477BF0  or          eax,0FFFFFFFFh
   237: }
07FF779477BF3  mov         rbx,qword ptr [rsp+30h]
07FF779477BF8  add         rsp,20h
07FF779477BFC  pop         rdi
07FF779477BFD  ret
   230:
   231:         return (int)(dataIter - dataBegin);
07FF779477BFE  sub         edx,ebx // dataIter = (int)dataIter - (int)dataBegin
07FF779477C00  mov         eax,edx // eax = dataIter
07FF779477C02  jmp         $nextDataPosition+0Bh (07FF779477BF3) // return eax