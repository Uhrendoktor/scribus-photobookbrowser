# Building against Scribus 1.6.5

1. Obtain the Scribus 1.6.5 source.
2. Run:

   ./install_into_scribus.sh /path/to/scribus-1.6.5

3. Open:

   /path/to/scribus-1.6.5/scribus/plugins/picbrowser/CMakeLists.txt

4. Copy its plugin target/helper declaration into:

   plugin/CMakeLists.txt

   and replace the source list with `PHOTOBOOKBROWSER_SOURCES`.

5. Reconfigure and build Scribus.

The first local compile is expected to identify any 1.6.5-specific method
signature differences in the isolated insertion adapter in
`photobookbrowserpalette.cpp`.
