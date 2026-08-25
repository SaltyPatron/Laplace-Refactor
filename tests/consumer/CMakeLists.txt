cmake_minimum_required(VERSION 3.22)
project(LaplaceConsumer LANGUAGES C)

find_package(Laplace 1 REQUIRED CONFIG)

add_executable(laplace_consumer main.c)
target_link_libraries(laplace_consumer PRIVATE Laplace::Engine)
