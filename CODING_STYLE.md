# Source formatting and function documentation

Project C/C++ sources use the checked-in `.clang-format` configuration. Function, namespace, type and control-flow block braces open on a new line and align vertically with their closing braces (Allman style). Short function/block bodies stay on separate lines. Include and using-declaration ordering is preserved. Existing unbraced single statements are retained.

Use Doxygen comments immediately before function definitions, before the template declaration for a template function. Document each overload, constructor and anonymous HTTP callback. Forward declarations refer to their documented definitions.

Each function comment includes:

- `@brief`: its purpose.
- `@details`: how it operates, including relevant validation, state changes or hardware interaction.
- `@param[in]`, `[out]` or `[in,out]`: the parameter name, its C/C++ type in parentheses and its meaning. Document unnamed adapter parameters explicitly.
- `@return`: the return type and result semantics, including failure results and pointer ownership where relevant. Void functions describe their observable effects. Functions without parameters say so explicitly.

```cpp
/**
 * @brief Check whether an interval has elapsed.
 * @details Subtract unsigned timestamps so elapsed-time checks tolerate rollover.
 * @param[in] now (uint32_t) Current monotonic time in milliseconds.
 * @param[in] previous (uint32_t) Timestamp of the previous operation.
 * @param[in] interval (uint32_t) Required elapsed interval in milliseconds.
 * @return (bool) True when at least interval milliseconds have elapsed.
 */
bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval)
{
    return static_cast<uint32_t>(now - previous) >= interval;
}
```

Python retains standard indentation instead of function/control-flow braces. Every function has a docstring with a purpose sentence and `Operation`, `Args` and `Returns` sections. Include the type of every documented argument and return value; test methods identify their test-case instance and assertion behavior.

Scope includes firmware and repository-owned test code. Generated build files, deprecated backup files and external libraries are excluded. Embedded HTML/CSS/JavaScript string content is preserved rather than passed through a C++ formatter as executable source.

Run `clang-format -i -style=file <source>` to format a file, or `clang-format --dry-run --Werror <source>` to check it. This pass used the installed clang-format 12.0.0. Continue running the ESP-01 build and relevant regression checks after edits; source-based tests should tolerate harmless whitespace changes.
