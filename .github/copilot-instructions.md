# Project Guidelines

## Language and Comments

- Write all source code, identifiers, strings intended for program output, and code documentation in English.
- Communicate with the user in Turkish unless they request another language.
- Add comments and documentation where they clarify intent, constraints, or non-obvious behavior. Keep comments accurate and avoid duplicating self-explanatory code.

## Library Layout

- Organize each library with `inc/` for public headers and `src/` for source files.
- Keep header files under `inc/` and implementation files under `src/`.
- Public headers may include only standard-library headers. Source files may include the library's own headers and other project headers.
- Keep platform-specific implementations separate where appropriate, while preserving a common public API.
- Design libraries to support Windows and Linux, and to allow extension to RTOS and bare-metal targets.

## Documentation and Verification

- Document every function in source files with a detailed Doxygen comment. Do not add function-level Doxygen comments to header declarations; keep function documentation in source files only. Use the project format: aligned `@brief`, `@details`, direction-qualified `@param`, `@return`, applicable `@retval` entries, and applicable `@note` and `@warning` tags. Document actual behavior and possible statuses; omit parameter tags only for functions without parameters.
- Make each file independently testable or verifiable where practical.
- Add or update tests for behavior changes, and use the project's build and test commands to validate changes.
