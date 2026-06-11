Reverse the following function into C++ that matches `IWD2.exe` (MFC/ATL, Win32 types — not modern C++).

**Target:** ${class_name}::${function_name} at ${address}

**Ghidra Decompile:**
```
${decompiled}
```

**Cross-references (calls from this function):**
${xrefs}

**Struct/type context:**
${structs}

**Existing source context:**
${source_context}

Requirements:
1. Match every branch and call from the decompile
2. Map offsets to real member names ONLY when context/source gives them; otherwise keep raw access. Keep `FUN_`/`DAT_` names verbatim — never invent a name or meaning
3. Preserve exact expression/operand order
4. Use existing project patterns and naming conventions
5. Output the complete function implementation in a ```cpp block
6. End with: REVERSED_FUNCTION: ${class_name}::${function_name} (${address})
