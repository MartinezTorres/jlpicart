#pragma once 

void vga320_init( std::function<const uint8_t *(uint16_t)> get_framebuffer_line = std::function<const uint8_t *(uint16_t)>() );

//extern uint8_t framebuffer[240][320];
