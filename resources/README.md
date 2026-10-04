# Application resources

DejaVu Sans is bundled unmodified with its redistribution notice in fonts/LICENSE.txt. The font is copied beside the application so no OS font path is required.

Application artwork is authored in icon/icon.png at the repository root. tools/prepare_icon.py regenerates its multi-size Windows ICO using Pillow. Windows embeds that ICO; SDL loads the PNG for the running window. Packaging includes the PNG and font resources.
