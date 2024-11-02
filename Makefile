

INCLUDE = -Iinclude -I/Users/macfarrell/VulkanSDK/1.3.296.0/macOS/include -I/opt/homebrew/Cellar/glfw/3.4/include
 
LINK = -L/opt/homebrew/Cellar/glfw/3.4/lib -L/Users/macfarrell/VulkanSDK/1.2.198.1/macOS/lib -lvulkan -lglfw -rpath /Users/macfarrell/VulkanSDK/1.2.198.1/macOS/lib/
 
debug: main.c
	clang main.c -o main $(INCLUDE) $(LINK) -g -O0