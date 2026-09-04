# Translations

AUDIT M25 scaffolding.

1. Extract strings (from a build tree with Qt Linguist tools):

   ```bash
   lupdate src -ts translations/biltoo_de.ts
   ```

2. Translate in Qt Linguist, then:

   ```bash
   lrelease translations/biltoo_de.ts -qm translations/biltoo_de.qm
   ```

3. Install `.qm` next to the binary under `translations/`, or embed via a
   future `i18n.qrc`. At runtime `main.cpp` loads:

   - `:/i18n/biltoo_*.qm` (if embedded)
   - `$appDir/translations/biltoo_*.qm`
   - `$prefix/share/biltoo/translations/biltoo_*.qm`

CLI help strings use `QCoreApplication::translate("main", …)`.
