#include <fstream>

#include "server.h"

#define PORT 12345

typedef struct ThreadData {
  DingleDots *dingle_dots;
  MultiThreadedServer *server;
  GSocketConnection *connection;
} ThreadData;

std::vector<std::string>
MultiThreadedServer::tokenizeString(const std::string &str, char delimiter) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token, token2;
  std::string quotedString;

  while (std::getline(iss, token, delimiter)) {
    quotedString.clear();
    if (token[0] == '"') {
      if (token.back() == '"') {
        // If the token starts and ends with quotes, treat it as a single quoted
        // string
        quotedString = token.substr(1, token.size() - 2); // Remove the quotes
      } else {
        quotedString =
            token.substr(1); // Start with the first part of the quoted string
        while (getline(iss, token2, delimiter)) {
          printf("Token2: %s\n", token2.c_str());
          printf("token2.back(): %c\n", token2.back());
          if (token2.back() != '"') {
            quotedString += delimiter + token2; // Append the next token
          } else {
            quotedString +=
                delimiter +
                token2.substr(0,
                              token2.size() - 1); // Continue appending until we
                                                  // find the closing quote
            break;
          }
        }
      }
      tokens.push_back(quotedString);
    } else {
      tokens.push_back(token);
    }
  }
  return tokens;
}

vwDrawable *MultiThreadedServer::parse_id(string id) {
  vwDrawable *drawable = nullptr;
  if (id.find("v4l2") != std::string::npos) {
    int v4l2_id =
        std::stoi(id.substr(4)); // Extract the numeric part after "v4l2"
    if (v4l2_id < 0 || v4l2_id >= MAX_NUM_V4L2) {
      g_message("Invalid V4L2 ID: %d", v4l2_id);
      return nullptr;
    }
    drawable = &dingle_dots->v4l2[v4l2_id];
  } else if (id.find("hex") != std::string::npos) {
    int hex_id =
        std::stoi(id.substr(3)); // Extract the numeric part after "hex"
    if (hex_id < 0 || hex_id >= MAX_NUM_TEXTS) {
      g_message("Invalid Hex ID: %d", hex_id);
      return nullptr;
    }
    drawable = &dingle_dots->hexes[hex_id];
  } else if (id.find("pic") != std::string::npos) {
    int pic_id =
        std::stoi(id.substr(3)); // Extract the numeric part after "pic"
    if (pic_id < 0 || pic_id >= MAX_NUM_SPRITES) {
      g_message("Invalid Pic ID: %d", pic_id);
      return nullptr;
    }
    drawable = &dingle_dots->sprites[pic_id];
  } else if (id.find("text") != std::string::npos) {
    int text_id =
        std::stoi(id.substr(4)); // Extract the numeric part after "pic"
    if (text_id < 0 || text_id >= MAX_NUM_TEXTS) {
      g_message("Invalid Pic ID: %d", text_id);
      return nullptr;
    }
    drawable = &dingle_dots->text[text_id];
  } else {
    g_message("Invalid ID format. Expected 'v4l2<number>' or 'hex<number>' or "
              "'pic<number>'.");
  }
  return drawable;
}

void *MultiThreadedServer::handle_client(void *data) {
  g_message("Handling client connection...");
  ThreadData *thread_data = (ThreadData *)data;
  GInputStream *istream =
      g_io_stream_get_input_stream(G_IO_STREAM(thread_data->connection));
  char buffer[1024];
  GError *error = NULL;
  gssize bytes_read;

  while ((bytes_read = g_input_stream_read(istream, buffer, sizeof(buffer) - 1,
                                           NULL, &error)) > 0) {
    buffer[bytes_read] = '\0'; // Null-terminate the string
    g_message("Received: %s", buffer);
    string message(buffer);
    std::vector<std::string> lines = tokenizeString(message, '\n');
    printf("Number of lines: %zu\n", lines.size());
    for (const auto &line : lines) {

      std::vector<std::string> commands = tokenizeString(line, ';');
      for (const auto &command : commands) {
        g_message("Processing command: %s", command.c_str());
        std::vector<std::string> tokens = tokenizeString(command);
        for (const auto &token : tokens) {
          g_message("Token: %s", token.c_str());
        }
        if (tokens.size() > 0) {
          if (tokens[0] == "quit") {
            g_message("Client requested to quit.");
            break;
          } else if (tokens[0] == "mv") {
            if (tokens.size() == 5) {
              string id = tokens[1];
              double x = std::stod(tokens[2]);
              double y = std::stod(tokens[3]);
              double duration = std::stod(tokens[4]);
              g_message("Moving %s to (%f, %f) over %f seconds", id, x, y,
                        duration);
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or is invalid.",
                          id.c_str());
                continue;
              }
              if (!drawable->active) {
                g_message("Drawable with ID %s is not active.", id.c_str());
                continue;
              }
              g_message(
                  "Moving drawable with ID %s to (%f, %f) over %f seconds",
                  id.c_str(), x, y, duration);
              Easer *easer_x = new Easer();
              easer_x->initialize(thread_data->dingle_dots, drawable,
                                  EASER_ELASTIC_EASE_OUT,
                                  std::bind(&vwDrawable::set_x, drawable,
                                            std::placeholders::_1),
                                  drawable->pos.x, x, duration);
              Easer *easer_y = new Easer();
              easer_y->initialize(thread_data->dingle_dots, drawable,
                                  EASER_ELASTIC_EASE_OUT,
                                  std::bind(&vwDrawable::set_y, drawable,
                                            std::placeholders::_1),
                                  drawable->pos.y, y, duration);
              easer_x->start();
              easer_y->start();
            } else {
              g_message("Invalid mv command format. Usage: mv <v4l2id> <x> <y> "
                        "<duration>");
            }
          } else if (tokens[0] == "rotate") {
            if (tokens.size() == 4) {
              string id = tokens[1];
              double angle = std::stod(tokens[2]);
              double duration = std::stod(tokens[3]);
              angle = 2 * M_PI * angle / 360.0; // Convert to radians
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or is invalid.",
                          id.c_str());
                continue;
              }
              g_message("Rotating %s  to %f", id, angle);
              Easer *easer_angle = new Easer();
              easer_angle->initialize(
                  thread_data->dingle_dots, drawable, EASER_CUBIC_EASE_IN_OUT,
                  std::bind(&vwDrawable::set_rotation, drawable,
                            std::placeholders::_1),
                  drawable->get_rotation(), angle, duration);
              easer_angle->start();
            } else {
              g_message("Invalid rotate command format. Usage: rotate <id> "
                        "<angle> <duration>");
            }
          } else if (tokens[0] == "makev4l2") {
            if (tokens.size() == 5) {
              char *name = strdup(tokens[1].c_str());
              double w = std::stod(tokens[2]);
              double h = std::stod(tokens[3]);
              bool mirrored = (tokens[4].find("true") != std::string::npos ||
                               tokens[4].find("1") != std::string::npos);
              if (w <= 0 || h <= 0) {
                g_message("Invalid dimensions for V4L2: (%f, %f)", w, h);
                continue;
              }
              if (strlen(name) >= DD_V4L2_MAX_STR_LEN) {
                g_message("V4L2 name too long. Max length is %d characters.",
                          DD_V4L2_MAX_STR_LEN - 1);
                continue;
              }
              // Check if the device name is valid
              g_message(
                  "Creating V4L2 %s with dimensions (%f, %f) mirrored: %d",
                  name, w, h, mirrored);
              for (int i = 0; i < MAX_NUM_V4L2; i++) {
                if (!thread_data->dingle_dots->v4l2[i].allocated &&
                    !thread_data->dingle_dots->v4l2[i].allocating) {
                  thread_data->dingle_dots->v4l2[i].init(
                      thread_data->dingle_dots, (char *)name, w, h, mirrored,
                      thread_data->dingle_dots->next_z++);
                  break;
                }
              }
            } else {
              g_message("Invalid makev4l2 command format. Usage: makev4l2 "
                        "<name> <width> <height> <mirrored>");
            }
          } else if (tokens[0] == "makehex") {
            if (tokens.size() == 8) {
              double x = std::stod(tokens[1]);
              double y = std::stod(tokens[2]);
              double w = std::stod(tokens[3]);
              double r = std::stod(tokens[4]);
              double g = std::stod(tokens[5]);
              double b = std::stod(tokens[6]);
              double a = std::stod(tokens[7]);

              if (w <= 0) {
                g_message("Invalid width for hex: (%f)", w);
                continue;
              }
              for (int i = 0; i < MAX_NUM_TEXTS; i++) {
                if (!thread_data->dingle_dots->hexes[i].allocated) {
                  vwColor c;
                  c.set_rgba(r, g, b, a); // Set color with full opacity
                  g_message("Creating hex at (%f, %f) with width %f", x, y, w);
                  thread_data->dingle_dots->hexes[i].create(
                      x, y, w, c, thread_data->dingle_dots);
                  break;
                }
              }
            } else {
              g_message("Invalid makehex command format. Usage: makehex <x> "
                        "<y> <width> <r> <g> <b>");
            }
          } else if (tokens[0] == "makepic") {
            if (tokens.size() == 4) {
              string filename = tokens[1];
              double x = std::stod(tokens[2]);
              double y = std::stod(tokens[3]);
              std::ifstream file(filename);
              if (!file.is_open()) {
                std::cerr << "Error opening file: " << filename << std::endl;
                continue; // Indicate an error
              }
              for (int i = 0; i < MAX_NUM_SPRITES; i++) {
                if (!thread_data->dingle_dots->sprites[i].allocated &&
                    !thread_data->dingle_dots->sprites[i].allocating) {
                  g_message("Creating sprite at (%f, %f)", x, y);
                  thread_data->dingle_dots->sprites[i].create(
                      &filename, thread_data->dingle_dots->next_z++,
                      thread_data->dingle_dots);
                  break;
                }
              }
            }
          } else if (tokens[0] == "setcolor") {
            if (tokens.size() == 7) {
              string id = tokens[1];
              double r = std::stod(tokens[2]);
              double g = std::stod(tokens[3]);
              double b = std::stod(tokens[4]);
              double a = std::stod(tokens[5]);
              double duration = std::stod(tokens[6]);
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or "
                          "is invalid.",
                          id.c_str());
                continue;
              }
              g_message("Setting color of %s to (%f, %f, %f)", id.c_str(), r, g,
                        b);

              Easer *easer_red = new Easer();
              easer_red->initialize(thread_data->dingle_dots, drawable,
                                    EASER_LINEAR,
                                    std::bind(&vwDrawable::set_color_red,
                                              drawable, std::placeholders::_1),
                                    drawable->c.get(R), r, duration);
              Easer *easer_green = new Easer();
              easer_green->initialize(
                  thread_data->dingle_dots, drawable, EASER_LINEAR,
                  std::bind(&vwDrawable::set_color_green, drawable,
                            std::placeholders::_1),
                  drawable->c.get(G), g, duration);
              Easer *easer_blue = new Easer();
              easer_blue->initialize(thread_data->dingle_dots, drawable,
                                     EASER_LINEAR,
                                     std::bind(&vwDrawable::set_color_blue,
                                               drawable, std::placeholders::_1),
                                     drawable->c.get(B), b, duration);
              Easer *easer_alpha = new Easer();
              easer_alpha->initialize(
                  thread_data->dingle_dots, drawable, EASER_LINEAR,
                  std::bind(&vwDrawable::set_color_alpha, drawable,
                            std::placeholders::_1),
                  drawable->c.get(A), a, duration);

              easer_red->start();
              easer_green->start();
              easer_blue->start();
              easer_alpha->start();
            } else {
              g_message("Invalid setcolor command format. Usage: "
                        "maketext <id> <r> <g> <b> <a> <duration>");
            }
          } else if (tokens[0] == "maketext") {
            printf("Tokens size: %zu\n", tokens.size());
            if (tokens.size() == 9) {
              string text = tokens[1];
              string font = tokens[2];
              double x = std::stod(tokens[3]);
              double y = std::stod(tokens[4]);
              double r = std::stod(tokens[5]);
              double g = std::stod(tokens[6]);
              double b = std::stod(tokens[7]);
              double a = std::stod(tokens[8]);
              if (text.empty() || font.empty()) {
                g_message("Text or font cannot be empty.");
                continue;
              }
              if (strlen(text.c_str()) >= STR_LEN) {
                g_message("Text too long. Max length is %d characters.",
                          STR_LEN - 1);
                continue;
              }
              if (strlen(font.c_str()) >= STR_LEN) {
                g_message("Font name too long. Max length is %d characters.",
                          STR_LEN - 1);
                continue;
              }
              vwColor c;
              c.set_rgba(r, g, b, a);
              g_message("Creating text '%s' with font '%s' and color (%f, %f, "
                        "%f, %f)",
                        text.c_str(), font.c_str(), r, g, b, a);
              for (int i = 0; i < MAX_NUM_TEXTS; i++) {
                if (!thread_data->dingle_dots->text[i].allocated &&
                    !thread_data->dingle_dots->text[i].allocating) {
                  g_message("Creating text at (%f, %f)", x, y);
                  thread_data->dingle_dots->text[i].create(
                      text.c_str(), font.c_str(), x, y, c,
                      thread_data->dingle_dots);
                  thread_data->dingle_dots->text[i].active = true;
                  break;
                }
              }
            } else {
              g_message("Invalid maketext command format. Usage: maketext "
                        "'<text>' '<font>' <x> <y>  <r> <g> <b> <a>");
            }
          } else if (tokens[0] == "snapshot") {
            if (tokens.size() == 1) {
              g_message("Taking snapshot...");
              thread_data->dingle_dots->do_snapshot = 1;
            } else {
              g_message("Invalid snapshot command format. Usage: snapshot");
            }
          } else if (tokens[0] == "scale") {
            if (tokens.size() == 4) {
              string id = tokens[1];
              double scale = std::stod(tokens[2]);
              double duration = std::stod(tokens[3]);
              if (scale <= 0) {
                g_message("Scale must be greater than 0.");
                continue;
              }
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or is invalid.",
                          id.c_str());
                continue;
              }
              g_message("Setting scale of %s to %f", id, scale);
              Easer *easer_scale = new Easer();
              easer_scale->initialize(
                  thread_data->dingle_dots, drawable, EASER_BOUNCE_EASE_OUT,
                  std::bind(&vwDrawable::set_scale, drawable,
                            std::placeholders::_1),
                  drawable->get_scale(), scale, duration);
              easer_scale->start();
            } else {
              g_message("Invalid scale command format. Usage: scale <id> "
                        "<value> <duration>");
            }
          } else if (tokens[0] == "opacity") {
            if (tokens.size() == 4) {
              string id = tokens[1];
              double opacity = std::stod(tokens[2]);
              double duration = std::stod(tokens[3]);
              if (opacity < 0 || opacity > 1) {
                g_message("Opacity must be between 0 and 1.");
                continue;
              }
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or is invalid.",
                          id.c_str());
                continue;
              }
              g_message("Setting opacity of %s to %f", id, opacity);
              Easer *easer_opacity = new Easer();
              easer_opacity->initialize(
                  thread_data->dingle_dots, drawable, EASER_LINEAR,
                  std::bind(&vwDrawable::set_opacity, drawable,
                            std::placeholders::_1),
                  drawable->get_opacity(), opacity, duration);
              easer_opacity->start();
            } else {
              g_message("Invalid opacity command format. Usage: opacity <id> "
                        "<value> <duration>");
            }
          } else if (tokens[0] == "delete") {
            if (tokens.size() == 2) {
              string id = tokens[1];
              g_message("Deleting %s", id);
              vwDrawable *drawable = thread_data->server->parse_id(id);
              if (drawable == nullptr) {
                g_message("Drawable with ID %s does not exist or is invalid.",
                          id.c_str());
                continue;
              }
              drawable->deactivate();
              drawable->allocated = 0;
            } else {
              g_message("Invalid delete command format. Usage: delete <id>");
            }
          } else if (tokens[0] == "fullscreen") {
            if (tokens.size() == 1) {
              thread_data->dingle_dots->toggle_fullscreen();
              g_message("Toggling fullscreen mode to %s",
                        !thread_data->dingle_dots->fullscreen ? "ON" : "OFF");
            } else {
              g_message("Invalid fullscreen command format. Usage: fullscreen");
            }
          } else if (tokens[0] == "help") {
            g_message("Available commands:\n"
                      "quit\n"
                      "mv <v4l2id> <x> <y> <duration>\n"
                      "rotate <id> <angle> <duration>\n"
                      "makev4l2 <name> <width> <height> <mirrored>\n"
                      "snapshot\n"
                      "opacity <id> <value> <duration>\n"
                      "delete <id>\n"
                      "fullscreen\n"
                      "help");
          }
          /*}  else if (tokens[0] == "addnote") {
              if (tokens.size() == 8) {
                  char *scale_name = strdup(tokens[1].c_str());
                  int scale_num = std::stoi(tokens[2]);
                  int midi_note = std::stoi(tokens[3]);
                  int midi_channel = std::stoi(tokens[4]);
                  double x = std::stod(tokens[5]);
                  double y = std::stod(tokens[6]);
                  double r = std::stod(tokens[7]);
                  color c = thread_data->dingle_dots->random_color();
                  g_message("Adding note: scale_name=%s, scale_num=%d,
             midi_note=%d, midi_channel=%d, x=%f, y=%f, r=%f", scale_name,
             scale_num, midi_note, midi_channel, x, y, r);
                  thread_data->dingle_dots->add_note(scale_name, scale_num,
             midi_note, midi_channel, x, y, r, &c); } else { g_message("Invalid
             addnote command format. Usage: addnote <scale_name> <scale_num>
             <midi_note> <midi_channel> <x> <y> <r>");
              }*/
          else if (tokens[0] == "addscale") {
            if (tokens.size() == 4) {

              char *key_name = strdup(tokens[1].c_str());
              int midi_base_note = std::stoi(tokens[2]);
              int midi_channel = std::stoi(tokens[3]);
              color c = thread_data->dingle_dots->random_color();
              int scaleid = midi_scale_text_to_id(key_name);
              midi_key_t key;
              midi_key_init_by_scale_id(&key, midi_base_note, scaleid);
              printf("midi_key: base_note=%d, scaleid=%d, num_steps=%d\n",
                     key.base_note, key.scaleid, key.num_steps);
              if (key.num_steps <= 0) {
                g_message("Invalid scale name: %s", key_name);
                g_free(key_name);
                continue;
              }
              if (midi_channel < 0 || midi_channel > 15) {
                g_message("Invalid MIDI channel: %d. Must be between 0 and 15.",
                          midi_channel);
                g_free(key_name);
                continue;
              }

              thread_data->dingle_dots->add_scale(&key, midi_channel, &c);
              g_message("Adding scale: key_name=%s, midi_base_note=%d, "
                        "midi_channel=%d",
                        key_name, midi_base_note, midi_channel);
              g_free(key_name);
            } else {
              g_message("Invalid addscale command format. Usage: addscale "
                        "<key_name> <<midi_channel>");
            }
          } else {
            g_message("Unknown command: %s", tokens[0].c_str());
          }
        }
      }
    }

    g_object_unref(thread_data->connection);
    return NULL;
  }
}
  void *MultiThreadedServer::handle_incoming_connection(
      GSocketService * service, GSocketConnection * connection,
      GObject * source_object, gpointer user_data) {
    MultiThreadedServer *server = static_cast<MultiThreadedServer *>(user_data);
    g_message("Received Connection from client!\n");
    pthread_t thread_id;
    ThreadData *thread_data = new ThreadData();
    thread_data->dingle_dots = server->dingle_dots;
    thread_data->connection = g_object_ref(connection);
    thread_data->server = server;
    g_message("Creating thread to handle client connection...");
    // Create a new thread to handle the client connection
    pthread_create(&thread_id, nullptr, handle_client, thread_data);
    return NULL;
  }

  // The server class
  MultiThreadedServer::MultiThreadedServer(DingleDots * dd) : dingle_dots(dd) {

    gboolean ret;
    GError *error = NULL;
    service = g_socket_service_new();
    ret = g_socket_listener_add_inet_port(G_SOCKET_LISTENER(service), PORT,
                                          NULL, &error);

    if (ret && error != NULL) {
      g_error("%s", error->message);
      g_clear_error(&error);
    }

    g_signal_connect(service, "incoming",
                     G_CALLBACK(handle_incoming_connection), this);

    g_socket_service_start(service);
  }
