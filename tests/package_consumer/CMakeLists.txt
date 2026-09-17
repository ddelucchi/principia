cmake_minimum_required(VERSION 3.25)

project(PrincipiaPackageConsumer LANGUAGES CXX)

find_package(Principia 0.1 CONFIG REQUIRED)

add_executable(principia_package_consumer main.cpp)
target_link_libraries(principia_package_consumer PRIVATE Principia::Game)
