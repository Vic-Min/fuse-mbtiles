#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <sqlite3.h>
#include <zlib.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <assert.h>
#if __cplusplus >= 201703L
#include <optional>
using std::optional;
#else
#include <boost/optional.hpp>
using boost::optional;
#endif
#include "Logger.h"
#ifdef USE_LOGGER
#include <unordered_map>
#endif //USE_LOGGER


static std::string mbtiles_filename;

static std::string ext;

// Whether or not to automatically compute the valid levels of the MBTiles file.
// By default this is false and will not scan the table to determine the min/max.
// This can take time when first loading the file so if you know the levels
// of your file up front you can set this to false and just use the min_level and
// max_level settings of the tile source.
static bool compute_levels = false;
static optional<int> minLevel;
static optional<int> maxLevel;

class Database
{
public:
	Database()
	{
		int rc = sqlite3_open_v2(mbtiles_filename.c_str(), &database_,
			SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
		if (rc != SQLITE_OK)
		{
			LOG_ERROR("sqlite3_open_v2 failed: %s", errmsg());
		}
	}

	~Database()
	{
		int rc = sqlite3_close(database_);
		if (rc != SQLITE_OK)
		{
			LOG_ERROR("sqlite3_close failed: %s", errmsg());
		}
	}

	operator sqlite3* ()
	{
		return database_;
	}

	const char* errmsg()
	{
		return sqlite3_errmsg(database_);
	}

private:
	sqlite3 * database_;
};


static optional<int> getMetaDataInt(Database& database, const char* key)
{
	LOG_TRACE("getMetaDataInt: key: %s", key);

	optional<int> ret;

	sqlite3_stmt* select = nullptr;
	const char* query = "SELECT value from metadata where name = ?";
	int rc = sqlite3_prepare_v2(database, query, -1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
		return ret;
	}

	rc = sqlite3_bind_text(select, 1, key, strlen(key), SQLITE_STATIC);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_bind_text failed: %s", database.errmsg());
		return ret;
	}

	rc = sqlite3_step(select);
	if (rc == SQLITE_ROW)
	{
		ret = sqlite3_column_int(select, 0);
	}
	else
	{
		LOG_ERROR("sqlite3_step failed: %s", database.errmsg());
		return ret;
	}

	sqlite3_finalize(select);

	return ret;
}

static optional<std::string> getMetaDataString(Database& database, const char* key)
{
	LOG_TRACE("getMetaDataString: key: %s", key);

	optional<std::string> ret;

	sqlite3_stmt* select = nullptr;
	const char* query = "SELECT value from metadata where name = ?";
	int rc = sqlite3_prepare_v2(database, query, -1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
		return ret;
	}

	rc = sqlite3_bind_text(select, 1, key, strlen(key), SQLITE_STATIC);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_bind_text failed: %s", database.errmsg());
		return ret;
	}

	rc = sqlite3_step(select);
	if (rc == SQLITE_ROW)
	{
		ret = reinterpret_cast<const char*>(sqlite3_column_text(select, 0));
	}
	else
	{
		LOG_ERROR("sqlite3_step failed: %s", database.errmsg());
		return ret;
	}

	sqlite3_finalize(select);

	return ret;
}


#define CHUNK 32768

static bool decompress(std::istream& fin, std::string& target)
{
	int ret;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	// allocate inflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, 15 + 32); // autodected zlib or gzip header

	if (ret != Z_OK)
	{
		LOG_ERROR("decompress: failed to init");
		return ret != 0;
	}

	// decompress until deflate stream ends or end of file
	do
	{
		fin.read(reinterpret_cast<char*>(in), CHUNK);
		strm.avail_in = fin.gcount();
		if (strm.avail_in == 0) break;

		// run inflate() on input until output buffer not full
		strm.next_in = in;
		do
		{
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);

			switch (ret)
			{
			case Z_NEED_DICT:
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&strm);
				return false;
			}
			unsigned have = CHUNK - strm.avail_out;
			target.append(reinterpret_cast<char*>(out), have);
		} while (strm.avail_out == 0);

		// done when inflate() says it's done
	} while (ret != Z_STREAM_END);

	// clean up and return
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? true : false;
}



static optional<std::string> getTile(Database& database, int zoom_level, int tile_column, int tile_row)
{
	LOG_TRACE("getTile: zoom_level: %i, tile_column: %i, tile_row: %i",
		zoom_level, tile_column, tile_row);

	assert(zoom_level >= 0);
	assert(tile_column >= 0);
	assert(tile_row >= 0);

	optional<std::string> ret;

	sqlite3_stmt* select = nullptr;
	int rc = sqlite3_prepare_v2(database,
		"SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?",
		-1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
		return ret;
	}

	sqlite3_bind_int(select, 1, zoom_level);
	sqlite3_bind_int(select, 2, tile_column);
	sqlite3_bind_int(select, 3, tile_row);

	if (sqlite3_step(select) == SQLITE_ROW)
	{
		const char* data = reinterpret_cast<const char*>(sqlite3_column_blob(select, 0));
		int len = sqlite3_column_bytes(select, 0);
		ret = std::string(data, len);
	}

	sqlite3_finalize(select);

	if (ret && ext == "pbf")
	{
		std::stringstream in(*ret);

		// Decompress the tile
		in.seekg(0, std::ios::beg);
		std::string value;
		if (decompress(in, value))
		{
			ret = value;
		}
		else
		{
			LOG_ERROR("decompress failed");
		}
	}

	return ret;
}


static int getTileOriginalSize(Database& database, int zoom_level, int tile_column, int tile_row)
{
	LOG_TRACE("getTileOriginalSize: zoom_level: %i, tile_column: %i, tile_row: %i",
		zoom_level, tile_column, tile_row);

	assert(zoom_level >= 0);
	assert(tile_column >= 0);
	assert(tile_row >= 0);

	int size = -1;

	sqlite3_stmt* select = nullptr;
	int rc = sqlite3_prepare_v2(database,
		"SELECT length(tile_data) FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?",
		-1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
		return -1;
	}
	sqlite3_bind_int(select, 1, zoom_level);
	sqlite3_bind_int(select, 2, tile_column);
	sqlite3_bind_int(select, 3, tile_row);

	if (sqlite3_step(select) == SQLITE_ROW)
		size = sqlite3_column_int(select, 0);

	sqlite3_finalize(select);

	return size;
}

static int getTileSize(Database& database, int zoom_level, int tile_column, int tile_row)
{
	LOG_TRACE("getTileSize: zoom_level: %i, tile_column: %i, tile_row: %i",
		zoom_level, tile_column, tile_row);

	if (ext == "pbf")
	{
		optional<std::string> tile = getTile(database, zoom_level, tile_column, tile_row);
		if (tile)
			return tile->size();
	}
	else
		return getTileOriginalSize(database, zoom_level, tile_column, tile_row);

	return -1;
}

void* mbtiles_init(struct fuse_conn_info *conn)
{
	LOG_TRACE("mbtiles_init: conn: %X", conn);

	Database database;

	minLevel = getMetaDataInt(database, "minzoom");
	if ( ! minLevel)
	{
		LOG_ERROR("getMetaData(minzoom) failed: %s", database.errmsg());
		return nullptr;
	}

	maxLevel = getMetaDataInt(database, "maxzoom");
	if ( ! maxLevel)
	{
		LOG_ERROR("getMetaData(maxzoom) failed: %s", database.errmsg());
		return nullptr;
	}

	optional<std::string> format = getMetaDataString(database, "format");
	if ( ! format)
	{
		LOG_ERROR("getMetaData(format) failed: %s", database.errmsg());
		return nullptr;
	}
	if ( ! (*format == "png" || *format == "jpg" || *format == "pbf"))
	{
		LOG_ERROR("unsupported format: %s", format->c_str());
		return nullptr;
	}

	ext = *format;

	return nullptr;
}

int mbtiles_getattr(const char *path, struct stat *stbuf)
{
	LOG_TRACE("mbtiles_getattr: path: %s", path);

	memset(stbuf, 0, sizeof(struct stat));

	int zoom_level = -1;
	int tile_column = -1;
	int tile_row = -1;
	sscanf(path, "/%i/%i/%i", &zoom_level, &tile_column, &tile_row);

	//	directory
	if (tile_row == -1)
	{
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_nlink = 2;
		return 0;
	}

	//	file
	Database database;

	tile_row = (1 << zoom_level) - 1 - tile_row;
	int len = getTileSize(database, zoom_level, tile_column, tile_row);
	if (len >= 0)
	{
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = len;

		return 0;
	}

	return -ENOENT;
}

int mbtiles_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	LOG_TRACE("mbtiles_readdir: path: %s", path);

	(void)offset;
	(void)fi;

	int zoom_level = -1;
	int tile_column = -1;
	int tile_row = -1;
	assert(path[0] == '/');
	sscanf(path, "/%i/%i/%i", &zoom_level, &tile_column, &tile_row);

	Database database;

	if (zoom_level == -1)
	{
		filler(buf, ".", nullptr, 0);
		filler(buf, "..", nullptr, 0);

		if ( ! compute_levels && minLevel && maxLevel)
		{
			for (int level = *minLevel; level <= *maxLevel; ++level)
				filler(buf, std::to_string(level).c_str(), nullptr, 0);
		}
		else
		{
			sqlite3_stmt* select = nullptr;
			int rc = sqlite3_prepare_v2(database,
				"SELECT DISTINCT zoom_level FROM tiles",
				-1, &select, nullptr);
			if (rc != SQLITE_OK)
			{
				LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
				return 1;
			}

			while (sqlite3_step(select) == SQLITE_ROW)
				filler(buf, reinterpret_cast<const char*>(sqlite3_column_text(select, 0)), nullptr, 0);

			sqlite3_finalize(select);
		}

		return 0;
	}


	if (tile_column == -1)
	{
		filler(buf, ".", nullptr, 0);
		filler(buf, "..", nullptr, 0);

		sqlite3_stmt* select = nullptr;
		int rc = sqlite3_prepare_v2(database,
			"SELECT DISTINCT tile_column FROM tiles WHERE zoom_level = ?",
			-1, &select, nullptr);
		if (rc != SQLITE_OK)
		{
			LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
			return 1;
		}

		sqlite3_bind_int(select, 1, zoom_level);

		while (sqlite3_step(select) == SQLITE_ROW)
			filler(buf, reinterpret_cast<const char*>(sqlite3_column_text(select, 0)), nullptr, 0);

		sqlite3_finalize(select);

		return 0;
	}

	if (tile_row == -1)
	{
		filler(buf, ".", nullptr, 0);
		filler(buf, "..", nullptr, 0);

		sqlite3_stmt* select = nullptr;
		int rc = sqlite3_prepare_v2(database,
			"SELECT tile_row FROM tiles WHERE zoom_level = ? AND tile_column = ?",
			-1, &select, nullptr);
		if (rc != SQLITE_OK)
		{
			LOG_ERROR("sqlite3_prepare_v2 failed: %s", database.errmsg());
			return 1;
		}

		sqlite3_bind_int(select, 1, zoom_level);
		sqlite3_bind_int(select, 2, tile_column);

		while (sqlite3_step(select) == SQLITE_ROW)
		{
			const int row = sqlite3_column_int(select, 0);
			std::string str = std::to_string((1 << zoom_level) - 1 - row) + "." + ext;
			filler(buf, str.c_str(), nullptr, 0);
		}

		sqlite3_finalize(select);

		return 0;
	}

	return -ENOENT;
}

int mbtiles_open(const char *path, struct fuse_file_info *fi)
{
	LOG_TRACE("mbtiles_open: path: %s", path);

	int zoom_level = -1;
	int tile_column = -1;
	int tile_row = -1;
	sscanf(path, "/%i/%i/%i.", &zoom_level, &tile_column, &tile_row);
	if (tile_row == -1)
		return -ENOENT;

	//	TODO: check data availability
	tile_row = (1 << zoom_level) - 1 - tile_row;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}


int mbtiles_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	LOG_TRACE("mbtiles_read: path: %s", path);

	(void)fi;
	int zoom_level = -1;
	int tile_column = -1;
	int tile_row = -1;
	sscanf(path, "/%i/%i/%i.", &zoom_level, &tile_column, &tile_row);

	assert(tile_row >= 0);
	tile_row = (1 << zoom_level) - 1 - tile_row;

	Database database;

	optional<std::string> tile = getTile(database, zoom_level, tile_column, tile_row);
	if ( ! tile || tile->size() <= offset)
		return 0;

	if (tile->size() < offset + size)
		size = tile->size() - offset;

	memcpy(buf, tile->data() + offset, size);

	return size;
}

#ifdef USE_LOGGER
static int createLogger(const char* logLevelStr, const char* logParamsStr)
{
	if ( ! logLevelStr)
		logLevelStr = getenv("FUSE_MBTILES_LOG_LEVEL");
	if ( ! logLevelStr)
		return 0;

	static const std::unordered_map<std::string, Logger::Level> logLevelNames =
	{{
		{"OFF",     Logger::LEVEL_OFF    },
		{"ERROR",   Logger::LEVEL_ERROR  },
		{"WARNING", Logger::LEVEL_WARNING},
		{"DEBUG",   Logger::LEVEL_DEBUG  },
		{"TRACE",   Logger::LEVEL_TRACE  },
	}};

	auto it = logLevelNames.find(logLevelStr);
	if (it == logLevelNames.end())
	{
		std::cerr << "invalid Log Level: " << logLevelStr << std::endl;
		return 1;
	}
	
	Logger::Level logLevel = it->second;
	if (logLevel == Logger::LEVEL_OFF)
		return 0;
	
	std::string logParams;
	if ( ! logParamsStr)
		logParamsStr = getenv("FUSE_MBTILES_LOG_PARAMS");
	if (logParamsStr)
		logParams = logParamsStr;

	logger = std::make_unique<Logger>(logLevel, logParams);
	if ( ! logger || ! logger->on(logLevel))
	{
		std::cerr << "can't create logger, level: " << logLevelStr << ", params: " << logParams << std::endl;
		return 1;
	}

	return 0;
}
#endif

struct options
{
	bool compute_levels = false;
	char *log_level = nullptr;
	char *log_params = nullptr;
} options;

enum {
	KEY_HELP,
};

#define OPT_DEF(t, p, v) { t, offsetof(struct options, p), v }
static struct fuse_opt options_desc[] =
{
	OPT_DEF("compute_levels",         compute_levels, 1),
	OPT_DEF("no_compute_levels",      compute_levels, 0),
	OPT_DEF("--compute_levels=true",  compute_levels, 1),
	OPT_DEF("--compute_levels=false", compute_levels, 0),
	OPT_DEF("log_level=%s",           log_level, 0),
	OPT_DEF("--log_level %s",         log_level, 0),
	OPT_DEF("log_params=%s",          log_params, 0),
	OPT_DEF("--log_params %s",        log_params, 0),

	FUSE_OPT_KEY("-h", KEY_HELP),
	FUSE_OPT_KEY("--help", KEY_HELP),

	FUSE_OPT_END,
};
#undef OPT_DEF

static void use(const char *prog_name)
{
	std::cerr << "use: " << prog_name << " [options] <mount_point> <mbtiles>" << std::endl;
	std::cerr <<
		"fuse_mbtiles options:\n"
		"    -o compute_levels     - compute the minzoom/maxzoom values from the 'tiles' table\n"
		"    -o no_compute_levels  - use the minzoom/maxzoom values from the 'metadata' table (default)\n"
		"    -o log_level=STRING   - must be OFF (default) | ERROR | WARNING | DEBUG | TRACE\n"
		"    -o log_params=STRING\n"
		"    --compute_levels=BOOL - same as 'compute_levels' or 'no_compute_levels'\n"
		"    --log_level STRING    - same as '-o log_level=STRING'\n"
		"    --log_params STRING   - same as '-o log_params=STRING'\n"
	;
}

static int opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	switch (key) {
	case KEY_HELP:
		use(outargs->argv[0]);
		fuse_opt_add_arg(outargs, "-h");
		fuse_main(outargs->argc, outargs->argv, static_cast<fuse_operations*>(nullptr), nullptr);
		exit(1);
	}
	return 1;
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	memset(&options, 0, sizeof(struct options));
	if (fuse_opt_parse(&args, &options, options_desc, opt_proc) == -1)
	{
		return -1;
	}

	if (args.argc < 3)
	{
		use(args.argv[0]);
		return 1;
	}

	int ret;
#ifdef USE_LOGGER
	ret = createLogger(options.log_level, options.log_params);
	if (ret)
		return ret;
#endif

	compute_levels = options.compute_levels || getenv("FUSE_MBTILES_COMPUTE_LEVELS");

	// last arg - mbtiles file name
	--args.argc;
	mbtiles_filename = args.argv[args.argc];

	fuse_operations mbtiles_oper{};
	mbtiles_oper.init = mbtiles_init;
	mbtiles_oper.getattr = mbtiles_getattr;
	mbtiles_oper.readdir = mbtiles_readdir;
	mbtiles_oper.open = mbtiles_open;
	mbtiles_oper.read = mbtiles_read;
	
	ret = fuse_main(args.argc, args.argv, &mbtiles_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
