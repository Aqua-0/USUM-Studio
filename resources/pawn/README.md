# Interaction support includes

`usum.inc` contains the native declarations and conversation helpers used by the interaction editor. It follows the application's license.

Generated `messages.inc` must be included first. It supplies the selected actor and allocated message IDs. The editor generates this file during compilation.

Configure a compatible version-10 `gf-pawncc` executable in the interaction editor's **Compiler settings**. The external compiler is not included here. Studio validates the resulting AMX version and supported relocation before staging.

Translated messages share IDs across the selected game languages. Name and number substitutions may be reordered in translations but must also appear in the English fallback.

The compiler is built separately from pinned source during release packaging; corresponding source and notices are included in the release artifacts.
