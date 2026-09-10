add_executable(gre_example_basic basic_figure.cpp)
target_link_libraries(gre_example_basic PRIVATE gre::gre)

add_executable(gre_example_hic hic_figure.cpp)
target_link_libraries(gre_example_hic PRIVATE gre::gre)

add_executable(gre_example_gallery gallery.cpp)
target_link_libraries(gre_example_gallery PRIVATE gre::gre)
