#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <sqlite3.h>
#include <zlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <assert.h>
#if __cplusplus >= 201703L
#include <optional>
using std::optional;
#else
#include <boost/optional.hpp>
using boost::optional;
#endif

static std::string mbtiles_filename;

static std::string ext;

constexpr char* LOG_FILE_NAME = "fuse-mbtiles.log";
static std::ofstream log;


class Database
{
public:
	Database()
	{
		int rc = sqlite3_open_v2(mbtiles_filename.c_str(), &database_,
			SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
		if (rc != SQLITE_OK)
		{
			log << "sqlite3_open_v2 failed: " << errmsg() << std::endl;
		}
	}

	~Database()
	{
		int rc = sqlite3_close(database_);
		if (rc != SQLITE_OK)
		{
			log << "sqlite3_close failed: " << errmsg() << std::endl;
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
	optional<int> ret;

	sqlite3_stmt* select = nullptr;
	const char* query = "SELECT value from metadata where name = ?";
	int rc = sqlite3_prepare_v2(database, query, -1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
		return ret;
	}

	rc = sqlite3_bind_text(select, 1, key, strlen(key), SQLITE_STATIC);
	if (rc != SQLITE_OK)
	{
		log << "sqlite3_bind_text failed: " << database.errmsg() << std::endl;
		return ret;
	}

	rc = sqlite3_step(select);
	if (rc == SQLITE_ROW)
	{
		ret = sqlite3_column_int(select, 0);
	}
	else
	{
		log << "sqlite3_step failed: " << database.errmsg() << std::endl;
		return ret;
	}

	sqlite3_finalize(select);

	return ret;
}

static optional<std::string> getMetaDataString(Database& database, const char* key)
{
	optional<std::string> ret;

	sqlite3_stmt* select = nullptr;
	const char* query = "SELECT value from metadata where name = ?";
	int rc = sqlite3_prepare_v2(database, query, -1, &select, nullptr);
	if (rc != SQLITE_OK)
	{
		log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
		return ret;
	}

	rc = sqlite3_bind_text(select, 1, key, strlen(key), SQLITE_STATIC);
	if (rc != SQLITE_OK)
	{
		log << "sqlite3_bind_text failed: " << database.errmsg() << std::endl;
		return ret;
	}

	rc = sqlite3_step(select);
	if (rc == SQLITE_ROW)
	{
		ret = reinterpret_cast<const char*>(sqlite3_column_text(select, 0));
	}
	else
	{
		log << "sqlite3_step failed: " << database.errmsg() << std::endl;
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
		log << "decompress: failed to init" << std::endl;
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
		log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
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
			log << "decompress failed" << std::endl;
		}
	}

	return ret;
}


static int getTileOriginalSize(Database& database, int zoom_level, int tile_column, int tile_row)
{
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
		log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
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
	Database database;

	optional<int> minLevel = getMetaDataInt(database, "minzoom");
	if ( ! minLevel)
	{
		log << "getMetaData(minzoom) failed: " << database.errmsg() << std::endl;
		return nullptr;
	}

	optional<int> maxLevel = getMetaDataInt(database, "maxzoom");
	if ( ! maxLevel)
	{
		log << "getMetaData(maxzoom) failed: " << database.errmsg() << std::endl;
		return nullptr;
	}

	optional<std::string> format = getMetaDataString(database, "format");
	if ( ! format)
	{
		log << "getMetaData(format) failed: " << database.errmsg() << std::endl;
		return nullptr;
	}
	if ( ! (*format == "png" || *format == "jpg" || *format == "pbf"))
	{
		log << "unsupported format: " << *format << std::endl;
		return nullptr;
	}

	ext = *format;

	return nullptr;
}

int mbtiles_getattr(const char *path, struct stat *stbuf)
{
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

		sqlite3_stmt* select = nullptr;
		int rc = sqlite3_prepare_v2(database,
			"SELECT DISTINCT zoom_level FROM tiles",
			-1, &select, nullptr);
		if (rc != SQLITE_OK)
		{
			log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
			return 1;
		}

		while (sqlite3_step(select) == SQLITE_ROW)
			filler(buf, reinterpret_cast<const char*>(sqlite3_column_text(select, 0)), nullptr, 0);

		sqlite3_finalize(select);

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
			log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
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
			log << "sqlite3_prepare_v2 failed: " << database.errmsg() << std::endl;
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


int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		std::cerr << "use: " << argv[0] << " [options] <mount_point> <mbtiles>" << std::endl;
		return 1;
	}

	log.open(LOG_FILE_NAME);
	if ( ! log)
	{
		std::cerr << "can't open file " << LOG_FILE_NAME << std::endl;
		return 1;
	}

	// last arg - mbtiles file name
	--argc;
	mbtiles_filename = argv[argc];

	fuse_operations mbtiles_oper{};
	mbtiles_oper.init = mbtiles_init;
	mbtiles_oper.getattr = mbtiles_getattr;
	mbtiles_oper.readdir = mbtiles_readdir;
	mbtiles_oper.open = mbtiles_open;
	mbtiles_oper.read = mbtiles_read;

	return fuse_main(argc, argv, &mbtiles_oper, NULL);
}
