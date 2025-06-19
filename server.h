#include "dingle_dots.h"
#include <iostream>

class MultiThreadedServer {
    public:
        MultiThreadedServer(DingleDots *dd);
        DingleDots *get_dingle_dots() const { return dingle_dots; }
        vwDrawable *parse_id(string id);
    private:
        static void *handle_client(void *data);
        static void *handle_incoming_connection(GSocketService *service,
                                   GSocketConnection *connection,
                                   GObject *source_object, gpointer user_data);
        static std::vector<std::string> tokenizeString(const std::string& str, char delimiter = ' ');
        
        GSocketService *service;
        DingleDots *dingle_dots;
}; 