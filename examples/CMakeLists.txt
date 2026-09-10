add_executable(gre_example_hic hic_figure.cpp)
target_link_libraries(gre_example_hic PRIVATE gre::gre)

add_executable(gre_example_square square_map.cpp)
target_link_libraries(gre_example_square PRIVATE gre::gre)
