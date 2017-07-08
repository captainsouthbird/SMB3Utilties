#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <direct.h>
#endif

#include "NoDiceLib.h"
#include "ezxml.h"
#include "internal.h"

#define CONFIG_XML	"config.xml"
#define GAME_XML	"game.xml"

static ezxml_t config_xml, game_xml;
struct NoDice_configuration NoDice_config;

static ezxml_t required_child(ezxml_t root, const char *item)
{
	ezxml_t child = NULL;

	if((child = ezxml_child(root, item)) == NULL)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Required configuration element \"%s\" not found", item);
	}

	return child;
}


static int count_children(ezxml_t xml, const char *child)
{
	ezxml_t node;
	int count = 0;

	for(node = ezxml_child(xml, child); node != NULL; node = ezxml_next(node))
		count++;

	return count;
}


static int attr_to_int(ezxml_t node, const char *attr_name, int default_val)
{
	const char *attr_value = ezxml_attr(node, attr_name);

	if(attr_value != NULL)
		return (int)strtol(attr_value, NULL, 0);
	else
		return default_val;
}


static unsigned char attr_to_byte(ezxml_t node, const char *attr_name, unsigned char default_val)
{
	return (unsigned char)attr_to_int(node, attr_name, default_val);
}


static void parse_generators(ezxml_t root, struct NoDice_generator *generator, int base_index, int gen_type)
{
	int cur_gen = base_index, i;
	ezxml_t node, pnode;

	// Iterate children...
	for(node = ezxml_child(root, "generator"); node != NULL; node = ezxml_next(node))
	{
		struct NoDice_generator *this_gen = &generator[cur_gen++];

		this_gen->id = attr_to_byte(node, "id", 0);
		this_gen->type = gen_type;
		this_gen->name = ezxml_attr(node, "name");
		this_gen->desc = ezxml_attr(node, "desc");

		// Iterate parameter descriptions
		for(i = 0, pnode = ezxml_child(node, "param"); i < GEN_MAX_PARAMS && pnode != NULL; i++, pnode = ezxml_next(pnode))
		{
			unsigned char max_default = (i == 0) ? 15 : 255;

			this_gen->parameters[i].name = ezxml_attr(pnode, "name");

			// Minimum value (default 0)
			this_gen->parameters[i].min = attr_to_byte(pnode, "min", 0);

			// Maximum value (default 15/255)
			this_gen->parameters[i].max = attr_to_byte(pnode, "max", max_default);
		}
	}
}


static void parse_levels(ezxml_t root, struct NoDice_the_levels **level, int *lev_count)
{
	struct NoDice_the_levels *lev = NULL;

	// Get count of levels
	*lev_count = count_children(root, "level");
	if(*lev_count > 0)
	{
		int cur_lev = 0;
		ezxml_t node;

		// Allocate that many variable-size levels
		lev = (struct NoDice_the_levels *)malloc(sizeof(struct NoDice_the_levels) * *lev_count);

		// Iterate children...
		for(node = ezxml_child(root, "level"); node != NULL; node = ezxml_next(node))
		{
			struct NoDice_the_levels *this_lev = &lev[cur_lev++];

			this_lev->name = ezxml_attr(node, "name");
			this_lev->layoutfile = ezxml_attr(node, "layoutfile");
			this_lev->layoutlabel = ezxml_attr(node, "layoutlabel");
			this_lev->objectfile = ezxml_attr(node, "objectfile");
			this_lev->objectlabel = ezxml_attr(node, "objectlabel");
			this_lev->desc = ezxml_attr(node, "desc");
		}

	}

	// Assign pointer on the way out
	*level = lev;
}


static void parse_tilehints(ezxml_t root, struct NoDice_tilehint **tilehints, int *tilehint_count)
{
	struct NoDice_tilehint *hints = NULL;

	// Get count of levels
	*tilehint_count = count_children(root, "tilehint");
	if(*tilehint_count > 0)
	{
		int cur_thint = 0;
		ezxml_t node;

		// Allocate that many variable-size levels
		hints = (struct NoDice_tilehint *)malloc(sizeof(struct NoDice_tilehint) * *tilehint_count);

		// Iterate children...
		for(node = ezxml_child(root, "tilehint"); node != NULL; node = ezxml_next(node))
		{
			struct NoDice_tilehint *this_hint = &hints[cur_thint++];

			this_hint->id 		= attr_to_byte(node, "id", 0);
			this_hint->overlay	= ezxml_attr(node, "overlay");
		}

	}

	// Assign pointer on the way out
	*tilehints = hints;
}


// Turn string of bit zeroes and ones into byte value
static unsigned char mask_bits_to_byte(const char *mask)
{
	unsigned char mask_value = 0;

	while(*mask != '\0')
		mask_value = (mask_value << 1) | (((*mask++) == '1') ? 1 : 0);

	return mask_value;
}

static void parse_header_options(ezxml_t root, struct NoDice_headers *header)
{
	int cur_options = 0;
	ezxml_t node, pnode;

	// Count <options /> blocks
	if((header->options_list_count = count_children(root, "options")) > 0)
	{
		// Allocate enough space for all <options />
		header->options_list = (struct NoDice_header_options *)calloc(header->options_list_count, sizeof(struct NoDice_header_options));

		// Iterate children...
		for(node = ezxml_child(root, "options"); node != NULL; node = ezxml_next(node))
		{
			struct NoDice_header_options *this_options = &header->options_list[cur_options++];
			const char *value;
			int cur_option = 0;

			// Id
			this_options->id = ezxml_attr(node, "id");

			// Mask
			if((value = ezxml_attr(node, "mask")) == NULL)
				// Not really useful
				this_options->mask = 0;
			else
				this_options->mask = mask_bits_to_byte(value);

			// Shift
			this_options->shift = attr_to_byte(node, "shift", 0);

			// Display
			this_options->display = ezxml_attr(node, "display");

			// Count how many <option /> blocks
			if((this_options->options_count = count_children(node, "option")) > 0)
			{
				// Allocate enough space for all <options />
				this_options->options = (struct NoDice_option *)calloc(this_options->options_count, sizeof(struct NoDice_option));

				// Iterate children...
				for(pnode = ezxml_child(node, "option"); pnode != NULL; pnode = ezxml_next(pnode))
				{
					struct NoDice_option *this_option = &this_options->options[cur_option++];

					this_option->label = ezxml_attr(pnode, "label");
					this_option->display = ezxml_attr(pnode, "display");
					this_option->value = attr_to_byte(pnode, "value", 0);
				}
			}

			// Show-If Id
			this_options->showif_id = ezxml_attr(node, "showif-id");

			// Show-If value
			if((value = ezxml_attr(node, "showif-val")) == NULL)
			{
				// Disables Show-If
				this_options->showif_id = NULL;
				this_options->showif_val = 0;
			}
			else
				this_options->showif_val = (unsigned char)strtoul(value, NULL, 0);

		}
	}
}


static void parse_objects(ezxml_t root, struct NoDice_objects *objects)
{
	ezxml_t node;

	// For all <object /> blocks...
	// Count number of object elements first
	int objects_count = count_children(root, "object");

	if(objects_count > 0)
	{
		// Iterate children...
		for(node = ezxml_child(root, "object"); node != NULL; node = ezxml_next(node))
		{
			int cur_sprite = 0;
			unsigned char id = attr_to_byte(node, "id", 0);
			struct NoDice_objects *this_obj = &objects[id];
			ezxml_t spr;

			this_obj->label = ezxml_attr(node, "label");
			this_obj->name = ezxml_attr(node, "name");
			this_obj->desc = ezxml_attr(node, "desc");

			// Parse out the <special /> block...
			parse_header_options(ezxml_child(node, "special"), &this_obj->special_options);

			this_obj->total_sprites = count_children(node, "sprite");
			this_obj->sprites = (struct NoDice_object_sprites *)calloc(1, sizeof(struct NoDice_object_sprites) * this_obj->total_sprites);

			// Iterate children...
			for(spr = ezxml_child(node, "sprite"); spr != NULL; spr = ezxml_next(spr))
			{
				struct NoDice_object_sprites *this_sprite = &this_obj->sprites[cur_sprite++];

				this_sprite->x = attr_to_int(spr, "x", 0);
				this_sprite->y = attr_to_int(spr, "y", 0);
				this_sprite->bank = attr_to_byte(spr, "bank", 0);
				this_sprite->pattern = attr_to_byte(spr, "pattern", 0);
				this_sprite->palette = attr_to_byte(spr, "palette", 0);
				this_sprite->flips |= (attr_to_byte(spr, "hflip", 0) != 0) ? 1 : 0;
				this_sprite->flips |= (attr_to_byte(spr, "vflip", 0) != 0) ? 2 : 0;
			}
		}
	}
}


// Comparison function for qsort to alphabetically order the generators
static int sort_gens_compare (const void * a, const void * b)
{
	const struct NoDice_generator *ga = (struct NoDice_generator *)a;
	const struct NoDice_generator *gb = (struct NoDice_generator *)b;

	const char *str_a = "", *str_b = "";

	if(ga != NULL && ga->name != NULL)
		str_a = ga->name;
	if(gb != NULL && gb->name != NULL)
		str_b = gb->name;

	return strcmp(str_a, str_b);
}


int _config_init()
{
	// Clear configuration structure
	memset(&NoDice_config, 0, sizeof(struct NoDice_configuration));

	// Attempt to load configuration XML
	if( ((config_xml = ezxml_parse_file(CONFIG_XML)) == NULL) || (config_xml->name == NULL) )
	{
		if(errno == ENOENT || errno == EACCES)
			// Handle file errors
			strncpy(_error_msg, "Unable to open " CONFIG_XML, sizeof(_error_msg));
		else
			snprintf(_error_msg, sizeof(_error_msg), "%s: %s", CONFIG_XML, ezxml_error(game_xml));
		return 0;
	}

	if((NoDice_config.game_dir = ezxml_attr(required_child(config_xml, "game"), "value")) == NULL)
		return 0;

	if((NoDice_config.filebase = ezxml_attr(required_child(config_xml, "filebase"), "value")) == NULL)
		return 0;

	if(strlen(NoDice_config.filebase) > (BUFFER_LEN - 4))
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "The specified \"filebase\" is too long!");
		return 0;
	}

	{
		const char *tokens = " \t";
		const char *build;
		size_t len;
		int count = 1;
		const char *token;

		if((build = ezxml_attr(required_child(config_xml, "build"), "value")) == NULL)
			return 0;

		// Form [game].asm
		snprintf(_buffer, BUFFER_LEN, "%s" EXT_ASM, NoDice_config.filebase);

		// Need to make a duplicate string... and add on [game].asm to it
		len = strlen(build) + strlen(_buffer) + 1;	// +1 for a space character
		NoDice_config.buildinfo._build_str = (char *)malloc(len+1);

		strcpy(NoDice_config.buildinfo._build_str, build);	// Start with build string
		strcat(NoDice_config.buildinfo._build_str, " ");		// Append a space
		strcat(NoDice_config.buildinfo._build_str, _buffer);	// Append the [game].asm

		// Don't parse string under Win32; CreateProcess takes a command line instead
#ifndef _WIN32
		// Tokenize string to get count of items in the execution string
		// FIXME: Won't handle grouping by quotation marks... problem??
		token = strtok(NoDice_config.buildinfo._build_str, tokens);
		while(strtok(NULL, tokens) != NULL)
			count++;

		// Allocate enough space for these strings...
		// +1: last pointer is always NULL, per execv requirements...
		NoDice_config.buildinfo.build_argv = (char **)calloc(count + 1, sizeof(char **));

		// Restart tokenization and fill the strings!
		strcpy(NoDice_config.buildinfo._build_str, build);	// Start with build string
		strcat(NoDice_config.buildinfo._build_str, " ");		// Append a space
		strcat(NoDice_config.buildinfo._build_str, _buffer);	// Append the [game].asm

		NoDice_config.buildinfo.build_argv[0] = strtok(NoDice_config.buildinfo._build_str, tokens);
		count = 1;
		while((token = strtok(NULL, tokens)) != NULL)
			NoDice_config.buildinfo.build_argv[count++] = token;
#endif
	}


	{
		const char *builderr;
		if((builderr = ezxml_attr(required_child(config_xml, "builderr"), "value")) == NULL)
			return 0;

		if(!strcasecmp(builderr, "returncode"))
			NoDice_config.buildinfo.builderr = BUILDERR_RETURNCODE;
		else if(!strcasecmp(builderr, "texterror"))
			NoDice_config.buildinfo.builderr = BUILDERR_TEXTERROR;
		else
		{
			snprintf(_error_msg, ERROR_MSG_LEN, "Unknown value for builderr: %s", builderr);
			return 0;
		}
	}


	{
		const char *core6502_timeout;
		if((core6502_timeout = ezxml_attr(required_child(config_xml, "coretimeout"), "value")) == NULL)
			return 0;
		NoDice_config.core6502_timeout = strtoul(core6502_timeout, NULL, 0);
	}

	{
		const char *level_range_check_high;
		if((level_range_check_high = ezxml_attr(required_child(config_xml, "levelrangecheckhigh"), "value")) == NULL)
			return 0;
		NoDice_config.level_range_check_high = (unsigned short)strtol(level_range_check_high, NULL, 0);
	}


	// Attempt to load game XML
	snprintf(_buffer, BUFFER_LEN, "%s/" GAME_XML, NoDice_config.game_dir);
	if( ((game_xml = ezxml_parse_file(_buffer)) == NULL) || (game_xml->name == NULL) )
	{
		if(errno == ENOENT || errno == EACCES)
			// Handle file errors
			snprintf(_error_msg, sizeof(_error_msg), "Unable to open %s", _buffer);
		else
			snprintf(_error_msg, sizeof(_error_msg), "%s: %s", _buffer, ezxml_error(game_xml));
		return 0;
	}
	else
	{
		ezxml_t game_node, node;

		// Parse <config />
		if((game_node = required_child(game_xml, "config")) == NULL)
			return 0;
		else
		{
			// Set defaults to configitems
			NoDice_config.game.options.title = "Untitled Game";
			NoDice_config.game.options.warpzone = "9";
			NoDice_config.game.options.object_set_bank = "prg006";

			// Iterate children...
			for(node = ezxml_child(game_node, "configitem"); node != NULL; node = ezxml_next(node))
			{
				const char *name = ezxml_attr(node, "name"), *value = ezxml_attr(node, "value");

				if(name != NULL && value != NULL)
				{
					if(!strcasecmp(name, "title"))
					{
						NoDice_config.game.options.title = value;
					}
					else if(!strcasecmp(name, "warpzone"))
					{
						NoDice_config.game.options.warpzone = value;
					}
					else if(!strcasecmp(name, "objectsetbank"))
					{
						NoDice_config.game.options.object_set_bank = value;
					}
				}
			}
		}

		// Parse <tilesets />
		if((game_node = required_child(game_xml, "tilesets")) == NULL)
			return 0;
		else
		{
			// Parse out the <tilehints />
			parse_tilehints(ezxml_child(game_node, "tilehints"), &NoDice_config.game.tilehints, &NoDice_config.game.tilehint_count);

			// Count number of tileset elements
			NoDice_config.game.tileset_count = count_children(game_node, "tileset");

			if(NoDice_config.game.tileset_count > 0)
			{
				int cur_tset = 0;

				// Allocate that many tilesets
				NoDice_config.game.tilesets = (struct NoDice_tileset *)calloc(1, sizeof(struct NoDice_tileset) * NoDice_config.game.tileset_count);

				// Iterate children...
				for(node = ezxml_child(game_node, "tileset"); node != NULL; node = ezxml_next(node))
				{
					int var_count;

					// For each <tileset /> ...
					struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[cur_tset++];

					// Copy relevant data
					tileset->id 		= attr_to_byte(node, "id", 0);
					tileset->name		= ezxml_attr(node, "name");
					tileset->path		= ezxml_attr(node, "path");
					tileset->rootfile	= ezxml_attr(node, "rootfile");
					tileset->desc		= ezxml_attr(node, "desc");

					// If omitted, "rootfile" = "path"
					if(tileset->rootfile == NULL)
					{
						tileset->rootfile = tileset->path;
					}

					// Count number of <vargenerators /> and <fixedgenerators />
					tileset->gen_count =
						(var_count =
						count_children(ezxml_child(node, "vargenerators"), "generator")) +
						count_children(ezxml_child(node, "fixedgenerators"), "generator");

					// Allocate that many generators
					tileset->generators =
						(struct NoDice_generator *)calloc(tileset->gen_count, sizeof(struct NoDice_generator));

					// Parse out the <vargenerators /> variable-size generators
					parse_generators(ezxml_child(node, "vargenerators"), tileset->generators, 0, GENTYPE_VARIABLE);

					// Parse out the <fixedgenerators /> fixed-size generators
					parse_generators(ezxml_child(node, "fixedgenerators"), tileset->generators, var_count, GENTYPE_FIXED);

					// Parse out the <tilehints />
					parse_tilehints(ezxml_child(node, "tilehints"), &tileset->tilehints, &tileset->tilehint_count);

					// Alphabetically sort the generators
					qsort(tileset->generators, tileset->gen_count, sizeof(struct NoDice_generator), sort_gens_compare);

					// Parse out the <levels /> levels
					parse_levels(ezxml_child(node, "levels"), &tileset->levels, &tileset->levels_count);
				}
			}
		}

		// Parse <levelheader />
		if((game_node = required_child(game_xml, "levelheader")) == NULL)
			return 0;
		else
		{
			int header_byte;

			// For all <headerx /> blocks...
			for(header_byte = 1; header_byte <= LEVEL_HEADER_COUNT; header_byte++)
			{
				ezxml_t hnode;
				snprintf(_buffer, BUFFER_LEN, "header%i", header_byte);

				// Parse out the <headerx /> block...
				if((hnode = ezxml_child(game_node, _buffer)) != NULL)
					parse_header_options(hnode, &NoDice_config.game.headers[header_byte-1]);
			}
		}


		// Parse <jctheader />
		if((game_node = required_child(game_xml, "jctheader")) == NULL)
			return 0;
		else
		{
			// Parse out the <jctheader /> block...
			parse_header_options(game_node, &NoDice_config.game.jct_options);
		}

		// Parse <objects />
		if((game_node = required_child(game_xml, "objects")) == NULL)
			return 0;
		else
		{
			parse_objects(game_node, NoDice_config.game.regular_objects);
		}

		// Parse <mapobjects />
		if((game_node = required_child(game_xml, "mapobjects")) == NULL)
			return 0;
		else
		{
			parse_objects(game_node, NoDice_config.game.map_objects);

			// <item /> blocks unique to map objects
			NoDice_config.game.total_map_object_items = count_children(game_node, "item");

			if(NoDice_config.game.total_map_object_items > 0)
			{
				int cur_item = 0;

				// Allocate that many map items
				NoDice_config.game.map_object_items = (struct NoDice_map_object_item *)calloc(1, sizeof(struct NoDice_map_object_item) * NoDice_config.game.total_map_object_items);

				// Iterate children...
				for(node = ezxml_child(game_node, "item"); node != NULL; node = ezxml_next(node))
				{
					// For each <item /> ...
					struct NoDice_map_object_item *item = &NoDice_config.game.map_object_items[cur_item++];

					// Copy relevant data
					item->id 		= attr_to_byte(node, "id", 0);
					item->name	= ezxml_attr(node, "name");
				}
			}
		}


		// Parse <maptiles />
		if((game_node = ezxml_child(game_xml, "maptiles")) != NULL)
		{
			NoDice_config.game.total_map_special_tiles = count_children(game_node, "tile");

			if(NoDice_config.game.total_map_special_tiles > MAX_SPECIAL_TILES)
			{
				snprintf(_error_msg, sizeof(_error_msg), "Maximum definable <maptiles> <tile> blocks is %i (this is an internal editor limit)", MAX_SPECIAL_TILES);
				return 0;
			}

			if(NoDice_config.game.total_map_special_tiles > 0)
			{
				int cur_tile = 0;

				// Allocate that many map items
				NoDice_config.game.map_special_tiles = (struct NoDice_map_special_tile *)calloc(1, sizeof(struct NoDice_map_special_tile) * NoDice_config.game.total_map_special_tiles);

				// Iterate children...
				for(node = ezxml_child(game_node, "tile"); node != NULL; node = ezxml_next(node))
				{
					// For each <tile /> ...
					struct NoDice_map_special_tile *tile = &NoDice_config.game.map_special_tiles[cur_tile++];

					ezxml_t opt_child;

					// Copy relevant data
					tile->id 		= attr_to_byte(node, "id", 0);

					if( (opt_child = ezxml_child(node, "tilelayout")) != NULL )
					{
						// We must pickup the "tileset" attribute!
						tile->tileset = attr_to_byte(opt_child, "tileset", 0);

						// Parse out the low and high option blocks...
						parse_header_options(ezxml_child(opt_child, "low"), &tile->override_tile.low);
						parse_header_options(ezxml_child(opt_child, "high"), &tile->override_tile.high);
					}

					if( (opt_child = ezxml_child(node, "objectlayout")) != NULL )
					{
						// Parse out the low and high option blocks...
						parse_header_options(ezxml_child(opt_child, "low"), &tile->override_object.low);
						parse_header_options(ezxml_child(opt_child, "high"), &tile->override_object.high);
					}
				}
			}
		}


	}


	// Backup CWD
	getcwd(NoDice_config.original_dir, PATH_MAX);

	// Change to game directory
	if(chdir(NoDice_config.game_dir) != 0)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Failed to change to game directory %s", NoDice_config.game_dir);
		return 0;
	}


	return 1;
}

void _config_shutdown()
{
	// Clean up configuration structure
	int i;

	// Change to original working directory
	if(chdir(NoDice_config.original_dir) != 0)
	{
		fprintf(stderr, "Failed to change to original directory %s", NoDice_config.original_dir);
	}

	// Free build strings
	if(NoDice_config.buildinfo._build_str != NULL)
		free(NoDice_config.buildinfo._build_str);

	if(NoDice_config.buildinfo.build_argv != NULL)
		free(NoDice_config.buildinfo.build_argv);

	if(NoDice_config.game.tilehints != NULL)
		free(NoDice_config.game.tilehints);

	if(NoDice_config.game.tilesets != NULL)
	{
		// Free components of tilesets
		for(i = 0; i < NoDice_config.game.tileset_count; i++)
		{
			struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[i];

			if(tileset->generators != NULL)
				free(tileset->generators);

			if(tileset->tilehints != NULL)
				free(tileset->tilehints);

			if(tileset->levels != NULL)
				free(tileset->levels);
		}

		// Free tilesets
		free(NoDice_config.game.tilesets);
	}

	for(i = 0; i < LEVEL_HEADER_COUNT; i++)
	{
		struct NoDice_headers *header = &NoDice_config.game.headers[i];
		if(header->options_list != NULL)
		{
			int j;

			// Free each <options /> block
			for(j = 0; j < header->options_list_count; j++)
			{
				struct NoDice_header_options *options_item = &header->options_list[j];

				// Free all <option /> blocks
				if(options_item->options != NULL)
					free(options_item->options);
			}

			// Free <options />
			free(header->options_list);
		}
	}

	if(NoDice_config.game.jct_options.options_list_count > 0)
		free(NoDice_config.game.jct_options.options_list);

	for(i = 0; i < 256; i++)
	{
		struct NoDice_objects *this_obj = &NoDice_config.game.regular_objects[i];

		if(this_obj->special_options.options_list_count > 0)
			free(this_obj->special_options.options_list);

		if(this_obj->total_sprites > 0)
			free(this_obj->sprites);
	}

	for(i = 0; i < 256; i++)
	{
		struct NoDice_objects *this_obj = &NoDice_config.game.map_objects[i];

		if(this_obj->special_options.options_list_count > 0)
			free(this_obj->special_options.options_list);

		if(this_obj->total_sprites > 0)
			free(this_obj->sprites);
	}

	// Free map object item defs
	if(NoDice_config.game.total_map_object_items > 0)
		free(NoDice_config.game.map_object_items);

	for(i = 0; i < NoDice_config.game.total_map_special_tiles; i++)
	{
		struct NoDice_map_special_tile *spec_tile = &NoDice_config.game.map_special_tiles[i];

		if(spec_tile->override_tile.low.options_list_count > 0)
			free(spec_tile->override_tile.low.options_list);

		if(spec_tile->override_tile.high.options_list_count > 0)
			free(spec_tile->override_tile.high.options_list);

		if(spec_tile->override_object.low.options_list_count > 0)
			free(spec_tile->override_object.low.options_list);

		if(spec_tile->override_object.high.options_list_count > 0)
			free(spec_tile->override_object.high.options_list);
	}

	if(NoDice_config.game.total_map_special_tiles > 0)
		free(NoDice_config.game.map_special_tiles);

	// Clear configuration structure (thus no need to set explicit NULLs)
	memset(&NoDice_config, 0, sizeof(struct NoDice_configuration));

	// These are not part of the configuration XML (are XMLs themselves!)
	// So they must be explicitly set to NULL after being freed...
	if(config_xml != NULL)
	{
		ezxml_free(config_xml);
		config_xml = NULL;
	}

	if(game_xml != NULL)
	{
		ezxml_free(game_xml);
		game_xml = NULL;
	}
}


// Add a level entry to a particular tileset
// Returns non-NULL string on error
const char *NoDice_config_game_add_level_entry(unsigned char tileset, const char *name, const char *layoutfile, const char *layoutlabel, const char *objectfile, const char *objectlabel, const char *desc)
{
	ezxml_t game_node, node;

	// Parse <tilesets />
	if((game_node = required_child(game_xml, "tilesets")) == NULL)
		return _error_msg;
	else
	{
		// Iterate children...
		for(node = ezxml_child(game_node, "tileset"); node != NULL; node = ezxml_next(node))
		{
			unsigned char id = attr_to_byte(node, "id", 0);

			if(tileset == id)
			{
				// Found the specific tileset... get to the levels branch!
				if((game_node = required_child(node, "levels")) == NULL)
					return _error_msg;
				else
				{
					// We're at the <levels /> ...
					// Since NoDice uses the XML's string memory and relies on that memory
					// to be managed as such, we must add this to XML immediately to use it,
					// even though it also must also be added to the internal structure...
					if(node = ezxml_add_child_d(game_node, "level", 0))
					{
						// Create the new child <level /> node
						ezxml_set_attr_d(node, "name", name);
						ezxml_set_attr_d(node, "layoutfile", layoutfile);
						ezxml_set_attr_d(node, "layoutlabel", layoutlabel);
						ezxml_set_attr_d(node, "objectfile", objectfile);
						ezxml_set_attr_d(node, "objectlabel", objectlabel);
						ezxml_set_attr_d(node, "desc", desc);

						// Write new game.xml
						{
							char *xml = ezxml_toxml(game_xml);
							FILE *game_f = fopen(GAME_XML, "wt");

							if(game_f != NULL)
							{
								fwrite(xml, sizeof(char), strlen(xml), game_f);
								fclose(game_f);
							}
							else
							{
								snprintf(_error_msg, ERROR_MSG_LEN, "Failed to write to %s", CONFIG_XML);
								return _error_msg;
							}
							free(xml);
						}


						// Pointers may have changed, memory moved, etc... gotta shutdown and reload config!
						_config_shutdown();

						if(!_config_init())
							return _error_msg;

						// Success!
						// Setup a pseudo-level so we can initiate a save on it (which creates the files, etc.)

						// Clear the level structure so no spurious things get saved
						memset(&NoDice_the_level, 0, sizeof(struct NoDice_level));

						// No error to return...
						return NULL;
					}
					else
					{
						snprintf(_error_msg, ERROR_MSG_LEN, "%s", ezxml_error(game_xml));
						return _error_msg;
					}
				}
			}
		}

		snprintf(_error_msg, ERROR_MSG_LEN, "Could not find tileset %i in the configuration XML!", tileset);
		return _error_msg;
	}

	// Shouldn't ever get here...
	return NULL;
}
