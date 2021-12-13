# fuse-mbtiles
Representation of the [MBTiles](https://github.com/mapbox/mbtiles-spec) file as an xyz file tree using [FUSE](https://wikipedia.org/wiki/Filesystem_in_Userspace).

Forming the contents of the root directory of the xyz tree requires scanning the entire MBTiles file, which can be time-consuming. To avoid this, you can use the minzoom/maxzoom values from the "metadata" table.  
This is the default behavior.  
To disable this and force the computation of the contents of the root directory, you must define the `FUSE_MBTILES_COMPUTE_LEVELS` environment variable with any non-empty value.
