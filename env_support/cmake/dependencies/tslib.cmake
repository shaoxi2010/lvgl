# ============================================================
# tslib Configuration
# ============================================================
set(CMAKE_PACKAGE_NAME "tslib")
set(PKG_LIB_PRIVATE "-lts")

option(LV_USE_FIND_PACKAGE_TSLIB "Resolve tslib via find_package"
       ${LV_USE_FIND_PACKAGE})
option(LV_USE_PKG_CONFIG_TSLIB "Resolve tslib via pkg-config"
       ${LV_USE_PKG_CONFIG})

if(LV_USE_FIND_PACKAGE_TSLIB)
  find_package(${CMAKE_PACKAGE_NAME} QUIET)
  if(tslib_FOUND)
    message(STATUS "lvgl: tslib: found via find_package")
    lvgl_link_packages(
      PRIVATE
      TARGETS
      tslib::tslib
      CMAKE_PACKAGE
      ${CMAKE_PACKAGE_NAME}
      PKG_CONFIG
      tslib
      PKG_LIB_PRIVATE
      ${PKG_LIB_PRIVATE})
    return()
  endif()
endif()

if(LV_USE_PKG_CONFIG_TSLIB AND PkgConfig_FOUND)
  # The pkg-config module is called "tslib" on most systems, but "ts" on some
  # older installations
  foreach(_pkg_config_name tslib ts)
    pkg_check_modules(TSLIB IMPORTED_TARGET QUIET ${_pkg_config_name})
    if(NOT TSLIB_FOUND)
      continue()
    endif()
    message(STATUS "lvgl: tslib: found via pkg-config")
    lvgl_link_pkg_config(
      PRIVATE
      TARGETS
      PkgConfig::TSLIB
      PKG_CONFIG
      ${_pkg_config_name}
      PKG_LIB_PRIVATE
      ${PKG_LIB_PRIVATE})
    return()
  endforeach()
endif()

message(FATAL_ERROR "lvgl: tslib not found. Please install tslib.")
