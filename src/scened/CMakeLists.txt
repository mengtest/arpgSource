 
######################## ��ĿԴ�ļ� ############################
file(GLOB_RECURSE sources_PCH PCH/*.cpp  PCH/*.h)
file(GLOB_RECURSE sources_Handler handler/*.cpp  handler/*.h)
file(GLOB_RECURSE sources_Object object/*.cpp  object/*.h)
file(GLOB_RECURSE sources_Server server/*.cpp  server/*.h)
file(GLOB_RECURSE sources_Scripts scripts/*.lua)
file(GLOB_RECURSE sources_Main *.cpp *.h *.c *.dist)
 
set(scened_SRCS
  ${sources_Object}
  ${sources_Server}
  ${sources_Handler}
  ${sources_Scripts}
)
set(scened_SRCS_ALL
  ${scened_SRCS}
  ${sources_Main}
)

if ( USE_COREPCH AND MSVC )
  set(scened_SRCS_ALL
	${scened_SRCS_ALL}
	${sources_PCH}
  )
endif()

if( WIN32 )
  set(scened_SRCS
	${scened_SRCS}
	${sources_Win32}
  )
endif()

######################## �����ļ� ########################
if( USE_COREPCH )
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()

include_directories(
	${CMAKE_BINARY_DIR}

	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/handler
	${CMAKE_CURRENT_SOURCE_DIR}/object
	${CMAKE_CURRENT_SOURCE_DIR}/server	
)

######################## ����ѡ�Ŀ�� ########################
set(prj_target cow_scened)
set(scened_LINK_FLAGS "")


##Ϊ�˱��ڵ�Ԫ�����Լ����̹���
add_executable(${prj_target} ${scened_SRCS_ALL})
set_target_properties(${prj_target} PROPERTIES LINK_FLAGS "${scened_LINK_FLAGS}")


######################## ��Ŀ������ ########################
set(libs
  ${cow_libs}
  svrcore
  ahocorasick  
  http
  luahttp
)

target_link_libraries(${prj_target} ${libs})

######################## Ԥ����֧�� #########################
# Generate precompiled header
if( USE_COREPCH )
  if(CMAKE_COMPILER_IS_GNUCXX)
	add_precompiled_header(${prj_target} ${CMAKE_CURRENT_SOURCE_DIR}/PCH/scenedPCH.h)
  elseif(MSVC)
	add_native_precompiled_header(${prj_target} ${CMAKE_CURRENT_SOURCE_DIR}/PCH/scenedPCH)
	string (REPLACE "/Zm1000" "/Zm500" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  endif()
endif()

######################## �������� ########################

if( UNIX )
  install(TARGETS ${prj_target} DESTINATION bin)
  install(FILES ${prj_target}.conf.dist DESTINATION etc)
elseif( WIN32 )
  install(TARGETS ${prj_target} DESTINATION "${CMAKE_INSTALL_PREFIX}")
  install(FILES ${prj_target}.conf.dist DESTINATION "${CMAKE_INSTALL_PREFIX}")
endif()

if( WIN32 )
  add_custom_command(TARGET ${prj_target}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${prj_target}.conf.dist ${CMAKE_BINARY_DIR}/bin/$(ConfigurationName)/
  )  
endif()

if (WIN32 AND MSVC)
  SOURCE_GROUP(main FILES ${sources_Main} ${sources_Win32} ${sources_PCH} ${sources_Debugging})
  #SOURCE_GROUP(pch FILES ${sources_Win32} ${sources_PCH} ${sources_Debugging})
  SOURCE_GROUP(object FILES ${sources_Object})
  SOURCE_GROUP(handler FILES ${sources_Handler})
  SOURCE_GROUP(server FILES ${sources_Server})
  SOURCE_GROUP(scripts FILES ${sources_Scripts})

  set_target_properties(${prj_target} PROPERTIES FOLDER "src") 
endif()

####################### ����֧�� ########################
#if(WITH_UNITTEST)
#  cxx_test3(${prj_target}
#	"${scened_SRCS}"
#	"${libs}"
#  )
#endif()