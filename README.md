# fuse-mbtiles
Representation of the [MBTiles](https://github.com/mapbox/mbtiles-spec) file as an xyz file tree using [FUSE](https://wikipedia.org/wiki/Filesystem_in_Userspace).

Forming the contents of the root directory of the xyz tree requires scanning the entire MBTiles file, which can be time-consuming. To avoid this, you can use the minzoom/maxzoom values from the "metadata" table.  
This is the default behavior.  
To disable this and force the computation of the contents of the root directory, you must define the `FUSE_MBTILES_COMPUTE_LEVELS` environment variable with any non-empty value.

CMake configure options:

- `USE_LOGGER` - Use logger (default OFF - the logger is not used)
- `LOGGER_DIR` - Logger `include` and `source` directory   (default `.`)
- `USE_LOGGER_P7` - Use logger P7 (default OFF - logging to a text file is used)
- `P7_INCLUDE_DIR` - P7 logger `include` directory (default `/usr/include/P7`)

Logging is configured by environment variables:

- `FUSE_MBTILES_LOG_LEVEL` - logging level. Possible values (each next level also includes messages issued at the previous level):
 - `OFF` - disabled (default)
 - `ERROR` - output only serious errors
 - `WARNING` - output warnings messages
 - `DEBUG` - output debug messages
 - `TRACE` - full trace

- `FUSE_MBTILES_LOG_PARAMS` - logger parameters:
 - if used P7 logger - see [P7 documentation](http://baical.net/p7.html)
 - else if logging to a text file is used - log file name

