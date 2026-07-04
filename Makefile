# ============================================================================
#  ft_irc — ircserv
# ============================================================================
NAME      := ircserv

CXX       := c++
CXXFLAGS  := -Wall -Wextra -Werror -std=c++98
DEPFLAGS  := -MMD -MP
INCLUDES  := -I include

SRCDIR    := src
OBJDIR    := obj

SRCS      := \
	$(SRCDIR)/main.cpp \
	$(SRCDIR)/Server.cpp \
	$(SRCDIR)/Client.cpp \
	$(SRCDIR)/Socket.cpp \
	$(SRCDIR)/CommandDispatcher.cpp

OBJS      := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEPS      := $(OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

# ヘッダ変更時のみ該当 .o を再ビルド（不要な再リンクを避ける：要件 N5）
-include $(DEPS)

.PHONY: all clean fclean re
