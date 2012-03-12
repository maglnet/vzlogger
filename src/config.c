/**
 * Parsing Apache HTTPd-like configuration
 *
 * @author Steffen Vogel <info@steffenvogel.de>
 * @copyright Copyright (c) 2011, The volkszaehler.org project
 * @package vzlogger
 * @license http://opensource.org/licenses/gpl-license.php GNU Public License
 */
/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#include <config.h>
#include <include/config.h>
#include "channel.h"
#include <VZException.hpp>


static const char *option_type_str[] = { "null", "boolean", "double", "int", "object", "array", "string" };

Config_Options::Config_Options()
    :  _config("/etc/vzlogger.conf")
    , _log("")
    , _port(8080)
    , _verbosity(0)
    , _comet_timeout(30)
    , _buffer_length(600)
    , _retry_pause(15)
    , _daemon(FALSE)
    , _local(FALSE)
    , _logging(TRUE)
{
	_logfd = NULL;
}

Config_Options::Config_Options(
  const std::string filename
  )
    : _config(filename)
    , _log("")
    , _port(8080)
    , _verbosity(0)
    , _comet_timeout(30)
    , _buffer_length(600)
    , _retry_pause(15)
    , _daemon(FALSE)
    , _local(FALSE)
    , _logging(TRUE)
{
	_logfd = NULL;
}

void Config_Options::config_parse(
  MapContainer::Ptr mappings
  ) {
	struct json_object *json_cfg = NULL;
	struct json_tokener *json_tok = json_tokener_new();

	char buf[JSON_FILE_BUF_SIZE];
	int line = 0;

	/* open configuration file */
	FILE *file = fopen(_config.c_str(), "r");
	if (file == NULL) {
		print(log_error, "Cannot open configfile %s: %s", NULL, _config.c_str(), strerror(errno)); /* why didn't the file open? */
		throw vz::VZException("Cannot open configfile.");
	}
	else {
		print(log_info, "Start parsing configuration from %s", NULL, _config.c_str());
	}

	/* parse JSON */
	while(fgets(buf, JSON_FILE_BUF_SIZE, file)) {
		line++;

		json_cfg = json_tokener_parse_ex(json_tok, buf, strlen(buf));

		if (json_tok->err > 1) {
			print(log_error, "Error in %s:%d %s at offset %d", NULL, _config.c_str(), line, json_tokener_errors[json_tok->err], json_tok->char_offset);
			throw vz::VZException("");
      exit(EXIT_FAILURE);
		}
	}

	/* householding */
	fclose(file);
	json_tokener_free(json_tok);

	try {
/* parse options */
	json_object_object_foreach(json_cfg, key, value) {
		enum json_type type = json_object_get_type(value);

		if (strcmp(key, "daemon") == 0 && type == json_type_boolean) {
			_daemon = json_object_get_boolean(value);
		}
		else if (strcmp(key, "foreground") == 0 && type == json_type_boolean) {
			_foreground = json_object_get_boolean(value);
		}
		else if (strcmp(key, "log") == 0 && type == json_type_string) {
			_log = strdup(json_object_get_string(value));
		}
		else if (strcmp(key, "retry") == 0 && type == json_type_int) {
			_retry_pause = json_object_get_int(value);
		}
		else if (strcmp(key, "verbosity") == 0 && type == json_type_int) {
			_verbosity = json_object_get_int(value);
		}
		else if (strcmp(key, "local") == 0) {
			json_object_object_foreach(value, key, local_value) {
				enum json_type local_type = json_object_get_type(local_value);

				if (strcmp(key, "enabled") == 0 && local_type == json_type_boolean) {
					_local = json_object_get_boolean(local_value);
				}
				else if (strcmp(key, "port") == 0 && local_type == json_type_int) {
					_port = json_object_get_int(local_value);
				}
				else if (strcmp(key, "timeout") == 0 && local_type == json_type_int) {
					_comet_timeout = json_object_get_int(local_value);
				}
				else if (strcmp(key, "buffer") == 0 && local_type == json_type_int) {
					_buffer_length = json_object_get_int(local_value);
				}
				else if (strcmp(key, "index") == 0 && local_type == json_type_boolean) {
					_channel_index = json_object_get_boolean(local_value);
				}
				else {
					print(log_error, "Ignoring invalid field or type: %s=%s (%s)",
						NULL, key, json_object_get_string(local_value), option_type_str[local_type]);
				}
			}
		}
		else if ((strcmp(key, "sensors") == 0 || strcmp(key, "meters") == 0) && type == json_type_array) {
			int len = json_object_array_length(value);
			for (int i = 0; i < len; i++) {
        Json::Ptr  jso(new Json(json_object_array_get_idx(value, i)));
				Map::Ptr mapping = config_parse_meter(jso);
				//if (mapping == NULL) {
				//	return ERR;
				//}
				mappings->push_back(*mapping.get());
			}
		}
		else {
			print(log_error, "Ignoring invalid field or type: %s=%s (%s)",
				NULL, key, json_object_get_string(value), option_type_str[type]);
		}
	}
  } catch (std::exception &e ) {
    json_object_put(json_cfg); /* free allocated memory */
    std::cout<<"parse configuration failed." << std::endl;
    std::cout << "Reason: " << e.what() << std::endl;
    throw;
  }
  
	print(log_debug, "Have %d meters.", NULL, mappings->size());
  
  
	//json_object_put(json_cfg); /* free allocated memory */
}

//Map::Ptr Config_Options::config_parse_meter(struct json_object *jso) {
Map::Ptr Config_Options::config_parse_meter(Json::Ptr jso) {
	//list_t json_channels;
	//list_init(&json_channels);
  //std::list<struct json_object> json_channels;
  std::list<Json> json_channels;
	std::list<Option> options;

	json_object_object_foreach(jso->Object(), key, value) {
		enum json_type type = json_object_get_type(value);

		if (strcmp(key, "channels") == 0 && type == json_type_array) {
			int len = json_object_array_length(value);
			for (int i = 0; i < len; i++) {
				//json_channels.push_back(*json_object_array_get_idx(value, i));
        json_channels.push_back(Json(json_object_array_get_idx(value, i)));
			}
		}
		else if (strcmp(key, "channel") == 0 && type == json_type_object) {
			json_channels.push_back(Json(value));
		}
		else { /* all other options will be passed to meter_init() */
			Option option(key, value);

			//if (option_init(value, key, option) != SUCCESS) {
			//	print(log_error, "Ignoring invalid type: %s=%s (%s)",
			//		NULL, key, json_object_get_string(value), option_type_str[type]);
			//}

			options.push_back(option);
		}
	}

	/* init meter */
	Map::Ptr  mapping(new Map(options));
  //list_init(&mapping->channels);

  //if (meter_init(&mapping->meter, options) != SUCCESS) {
		//print(log_error, "Failed to initialize meter. Arborting.", mapping);
		//free(mapping);
		//return NULL;
	//}

	print(log_info, "New meter initialized (protocol=%s)", NULL/*(mapping*/,
        meter_get_details(mapping->meter()->protocolId())->name);

	/* init channels */
  for(std::list<Json>::iterator it=json_channels.begin();
      it!= json_channels.end(); it++) {
    config_parse_channel(*it, mapping);
	}

	/* householding */
	//list_free(&options);
  std::cout<< "Return mapping...\n";
	return mapping;
}

void Config_Options::config_parse_channel(Json &jso, Map::Ptr mapping)
{
	const char *uuid = NULL;
	const char *middleware = NULL;
	const char *id_str = NULL;

  print(log_debug, "Configure channel.", NULL);
	json_object_object_foreach(jso.Object(), key, value) {
		enum json_type type = json_object_get_type(value);

		if (strcmp(key, "uuid") == 0 && type == json_type_string) {
			uuid = json_object_get_string(value);
		}
		else if (strcmp(key, "middleware") == 0 && type == json_type_string) {
			middleware = json_object_get_string(value);
		}
		else if (strcmp(key, "identifier") == 0 && type == json_type_string) {
			id_str = json_object_get_string(value);
		}
		else {
			print(log_error, "Ignoring invalid field or type: %s=%s (%s)",
				NULL, key, json_object_get_string(value), option_type_str[type]);
		}
	}

	/* check uuid and middleware */
	if (uuid == NULL) {
		print(log_error, "Missing UUID", NULL);
		throw vz::VZException("Missing UUID");
	}
	else if (!config_validate_uuid(uuid)) {
		print(log_error, "Invalid UUID: %s", NULL, uuid);
		throw vz::VZException("Invalid UUID.");
	}
	else if (middleware == NULL) {
		print(log_error, "Missing middleware", NULL);
		throw vz::VZException("Missing middleware.");
	}

	/* parse identifier */
	ReadingIdentifier::Ptr id;
  try {
    if( id_str != NULL ) {
      id = reading_id_parse(mapping->meter()->protocolId(), (const char *)id_str);
    }
  } catch ( vz::VZException &e ) {
		print(log_error, "Invalid id: %s", NULL, id_str);
		throw vz::VZException("Invalid reader.");
  }
  
	Channel ch(uuid, middleware, id);
	print(log_info, "New channel initialized (uuid=...%s middleware=%s id=%s)", NULL/*ch*/,
        uuid+30, middleware, (id_str) ? id_str : "(none)");
  mapping->push_back(ch);
  std::cout<<"pushed new channel....\n";
  
}

int config_validate_uuid(const char *uuid) {
	for (const char *p = uuid; *p; p++) {
		switch (p - uuid) {
			case 8:
			case 13:
			case 18:
			case 23:
				if (*p != '-') return FALSE;
				else break;

			default:
				if (!isxdigit(*p)) return FALSE;
				else break;
		}
	}
	return TRUE;
}
