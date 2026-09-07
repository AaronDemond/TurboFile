cmake_minimum_required(VERSION 3.19)
project(TurboFIle LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(TurboFIle
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow_navigation.cpp
    mainwindow_tabs.cpp
    mainwindow_fileactions.cpp
    mainwindow_fileview.cpp

    # The source model calculates directory sizes in the background, the proxy
    # provides directory-first sorting, and the header gates incomplete sorts.
    directorysizefilesystemmodel.cpp
    directorysizefilesystemmodel.h
    directorysizeheaderview.cpp
    directorysizeheaderview.h
    directorysizesortproxymodel.cpp
    directorysizesortproxymodel.h

    # Highlighted Bash editor and asynchronous process runner opened from the
    # file-view context menu.
    shellscriptdialog.cpp
    shellscriptdialog.h

    # Window-level pinned-directory sidebar.
    sidebar/pinnedsidebar.cpp
    sidebar/pinnedsidebar.h
    sidebar/pinnedlistwidget.cpp
    sidebar/pinnedlistwidget.h
    sidebar/pinneditemwidget.cpp
    sidebar/pinneditemwidget.h
    sidebar/pinnedpath.h
    sidebar/pinnedconstants.h
    sidebar/pinnedfoldericon.cpp
    sidebar/pinnedfoldericon.h
    sidebar/pincolorpresets.h
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(TurboFIle
    PRIVATE
        Qt::Core
        Qt::Widgets
)

include(GNUInstallDirs)

install(TARGETS TurboFIle
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET TurboFIle
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
