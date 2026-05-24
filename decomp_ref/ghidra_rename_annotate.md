# Ghidra rename / annotate workflow (GhidraMCP)

Ghidra DB is the source of truth for function names, signatures, locals, params, comments, bookmarks, and tags.
Use the **GhidraMCP REST API** (`http://127.0.0.1:8089`) — run Ghidra GUI, plugin auto-starts.

## Start / health

```powershell
# sanity checks
curl -s http://127.0.0.1:8089/check_connection
curl -s "http://127.0.0.1:8089/get_metadata"
```

No separate server to start — the plugin binds `:8089` when Ghidra GUI opens.

## Inspect before mutating

```powershell
# Decompile to pseudocode
curl -s "http://127.0.0.1:8089/decompile_function?address=0x5D2DE0"

# Disassembly
curl -s "http://127.0.0.1:8089/disassemble_function?address=0x5D2DE0"

# List locals + params
curl -s "http://127.0.0.1:8089/get_function_variables?address=0x5D2DE0"

# Current signature
curl -s "http://127.0.0.1:8089/get_function_signature?address=0x5D2DE0"

# Callers / callees
curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x5D2DE0&limit=20"
curl -s "http://127.0.0.1:8089/get_xrefs_from?address=0x5D2DE0&limit=10"

# Bookmarks at address
curl -s "http://127.0.0.1:8089/list_bookmarks?address=0x5D2DE0"

# Search functions by name pattern
curl -s "http://127.0.0.1:8089/search_functions?name_pattern=RenderFog&limit=50"
```

## Rename function

```powershell
# By address (preferred — stable after renames)
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","new_name":"RenderFogOfWar"}'

# By old name
curl -s -X POST http://127.0.0.1:8089/rename_function \
  -H "Content-Type: application/json" \
  -d '{"oldName":"sub_5D2DE0","newName":"RenderFogOfWar"}'
```

### Set function prototype

Note: `set_function_prototype` does NOT rename the function — rename first, then set prototype.

```powershell
curl -s -X POST http://127.0.0.1:8089/set_function_prototype \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","prototype":"void RenderFogOfWar(CVidMode* pVidMode)"}'
```

For `__thiscall`:
```powershell
curl -s -X POST http://127.0.0.1:8089/set_function_prototype \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","prototype":"void __thiscall RenderFogOfWar(CVidMode* pVidMode)"}'
```

## Rename/retype locals and params

```powershell
# Rename local variable
curl -s -X POST http://127.0.0.1:8089/rename_variable \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","oldName":"local_8","newName":"pArea"}'

# Rename parameter
curl -s -X POST http://127.0.0.1:8089/rename_variable \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","oldName":"param_1","newName":"pVidMode"}'

# Set local variable type
curl -s -X POST http://127.0.0.1:8089/set_local_variable_type \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","variable_name":"pArea","new_type":"CGameArea *"}'

# Set parameter type
curl -s -X POST http://127.0.0.1:8089/set_parameter_type \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x5D2DE0","parameter_name":"pVidMode","new_type":"CVidMode *"}'
```

## Batch rename (function + components atomically)

```powershell
curl -s -X POST http://127.0.0.1:8089/batch_rename_function_components \
  -H "Content-Type: application/json" \
  -d '{
    "function_address":"0x5D2DE0",
    "function_name":"RenderFogOfWar",
    "parameter_renames":{"param_1":"pVidMode"},
    "local_renames":{"local_8":"pArea"},
    "return_type":"void"
  }'
```

## Comments (plate / decompiler / disassembly)

```powershell
# Plate comment (function header)
curl -s -X POST http://127.0.0.1:8089/set_plate_comment \
  -H "Content-Type: application/json" \
  -d '{"address":"0x5D2DE0","comment":"Renders fog of war overlay."}'

# Decompiler PRE_COMMENT at inner address
curl -s -X POST http://127.0.0.1:8089/set_decompiler_comment \
  -H "Content-Type: application/json" \
  -d '{"address":"0x5D2E40","comment":"alpha blend setup"}'

# Disassembly EOL_COMMENT
curl -s -X POST http://127.0.0.1:8089/set_disassembly_comment \
  -H "Content-Type: application/json" \
  -d '{"address":"0x5D2E40","comment":"load pixel pitch"}'

# Batch set multiple comments
curl -s -X POST http://127.0.0.1:8089/batch_set_comments \
  -H "Content-Type: application/json" \
  -d '{
    "address":"0x5D2DE0",
    "plate_comment":"Fog of war renderer",
    "decompiler_comments":[{"address":"0x5D2E40","comment":"blend setup"}]
  }'

# Get plate comment
curl -s "http://127.0.0.1:8089/get_plate_comment?address=0x5D2DE0"
```

## Bookmarks

```powershell
# Create/update bookmark
curl -s -X POST http://127.0.0.1:8089/set_bookmark \
  -H "Content-Type: application/json" \
  -d '{"address":"0x5D2DE0","category":"review","comment":"Check blend flags."}'

# Delete bookmark
curl -s -X POST http://127.0.0.1:8089/delete_bookmark \
  -H "Content-Type: application/json" \
  -d '{"address":"0x5D2DE0","category":"review"}'

# List all bookmarks
curl -s "http://127.0.0.1:8089/list_bookmarks"

# List bookmarks by category
curl -s "http://127.0.0.1:8089/list_bookmarks?category=TODO"
```

## Function tags

```powershell
# Add tag(s) to function
curl -s -X POST http://127.0.0.1:8089/add_function_tag \
  -H "Content-Type: application/json" \
  -d '{"function":"0x5D2DE0","tags":"reviewed"}'

# Add multiple tags
curl -s -X POST http://127.0.0.1:8089/add_function_tag \
  -H "Content-Type: application/json" \
  -d '{"function":"0x5D2DE0","tags":"reviewed,complete"}'

# Update tag comment/description
curl -s -X POST http://127.0.0.1:8089/set_function_tag_comment \
  -H "Content-Type: application/json" \
  -d '{"name":"reviewed","comment":"manual RE pass complete"}'

# Remove tag
curl -s -X POST http://127.0.0.1:8089/remove_function_tag \
  -H "Content-Type: application/json" \
  -d '{"function":"0x5D2DE0","tags":"reviewed"}'

# List all tags
curl -s "http://127.0.0.1:8089/list_function_tags"

# Search functions by tag
curl -s "http://127.0.0.1:8089/search_functions_by_tag?tag=reviewed&limit=50"
```

## Batch operations

```powershell
# Batch add tags
curl -s -X POST http://127.0.0.1:8089/batch_add_function_tags \
  -H "Content-Type: application/json" \
  -d '{"assignments":[{"function":"0x5D2DE0","tags":"reviewed"},{"function":"0x5D3000","tags":"reviewed"}]}'
```

## Persist Ghidra DB

```powershell
# Save current program
curl -s "http://127.0.0.1:8089/save_program?program=IWD2.exe"

# Save all open programs
curl -s "http://127.0.0.1:8089/save_all_programs"
```

Mutations are in-memory until saved. Always save after a batch of renames.

## Import source TODO/FIXME as Ghidra bookmarks

Uses `run_script_inline` with a custom Ghidra script (Java) that parses `src/` for `// TODO` / `// FIXME` comments and creates bookmarks at the matching function addresses.

Current import: 716 source bookmarks (415 TODO, 301 FIXME).

## Batch rename file (JSON)

For programmatic batch renaming, compose a JSON array and POST to individual endpoints or use the batch endpoints:

```json
[
  {"endpoint":"rename_function_by_address", "body":{"function_address":"0x5D2DE0","new_name":"RenderFogOfWar"}},
  {"endpoint":"rename_variable", "body":{"function_address":"0x5D2DE0","oldName":"local_8","newName":"pArea"}},
  {"endpoint":"set_local_variable_type", "body":{"function_address":"0x5D2DE0","variable_name":"pArea","new_type":"CGameArea *"}},
  {"endpoint":"set_plate_comment", "body":{"address":"0x5D2DE0","comment":"Fog overlay renderer"}},
  {"endpoint":"set_bookmark", "body":{"address":"0x5D2DE0","category":"review","comment":"Compare against BG2 renderer"}}
]
```

## Source sync rule

After function rename in Ghidra:

```powershell
rg "sub_5D2DE0|FUN_005D2DE0|OldName" src/
```

Then update declaration, definition, and callsites atomically. Build before commit.

Fields are not stored in Ghidra DB; field names in C++ headers remain source-of-truth, and must be renamed class-scoped only.

## Choosing address over name

Always prefer `function_address` (0x hex) over `function_name` in API calls — it's stable even if the function was just renamed and name lookup hasn't caught up yet. Some endpoints use `address` as the param name for addresses (< 0x80000000), others use `function_address` (~0x14... range). When in doubt, check the endpoint schema.
