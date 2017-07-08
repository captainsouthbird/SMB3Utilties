#include "NoDiceLib.h"
#include "ezxml.h"
#include "MusConv.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_XML	"musconv.xml"

struct _import_map import_patch_map[128], import_noise_map[128], import_percussion_map[128];

static ezxml_t required_child(ezxml_t root, const char *item)
{
	ezxml_t child = NULL;

	if((child = ezxml_child(root, item)) == NULL)
	{
		fprintf(stderr, "Required configuration element \"%s\" not found", item);
	}

	return child;
}


static void process_import_map(ezxml_t root, struct _import_map *import_map, const char *map_name)
{
	ezxml_t node;

	for(node = ezxml_child(root, map_name); node != NULL; node = ezxml_next(node))
	{
		const char *id = ezxml_attr(node, "id");
		const char *map = ezxml_attr(node, "map");

		if(id == NULL)
			fprintf(stderr, "WARNING: <%s /> element missing required element \"id\"; ignoring\n", map_name);
		else if(map == NULL)
			fprintf(stderr, "WARNING: <%s /> element missing required element \"map\"; ignoring\n", map_name);

		import_map[strtoul(id, NULL, 0)].map = (unsigned char)strtoul(map, NULL, 0);
	}
}


int config_load()
{
	ezxml_t config_xml, root;

	// Clear configuration structure
	memset(import_patch_map, 0, sizeof(import_patch_map));
	memset(import_percussion_map, 0, sizeof(import_percussion_map));

	// Attempt to load configuration XML
	if((config_xml = ezxml_parse_file(CONFIG_XML)) == NULL)
	{
		if(errno == ENOENT || errno == EACCES)
			// Handle file errors
			fprintf(stderr, "Unable to open " CONFIG_XML);
		else
			fprintf(stderr, ezxml_error(config_xml));
		return 0;
	}

	if((root = required_child(config_xml, "patches")) == NULL)
		return 0;

	process_import_map(root, import_patch_map, "patch");

	if((root = required_child(config_xml, "noises")) == NULL)
		return 0;

	process_import_map(root, import_noise_map, "noise");

	if((root = required_child(config_xml, "percussions")) == NULL)
		return 0;

	process_import_map(root, import_percussion_map, "percussion");

	ezxml_free(config_xml);
	return 1;
}
