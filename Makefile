NAME = webserv

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

SRC_DIR = src
OBJ_DIR = build/obj

# Recursively find all .cpp files in src directory
SRCS = $(wildcard src/*.cpp src/*/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

INCLUDES = -I includes
LDFLAGS =

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf build

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
