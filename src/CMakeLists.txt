idf_component_register(SRCS "SocketServer.cpp"
                            "Misc.cpp"
                            "Listener.cpp"
                            "HSPI.cpp"
                            "Connection.cpp"

                    INCLUDE_DIRS "." "include" REQUIRES arduino wifi_provisioning)