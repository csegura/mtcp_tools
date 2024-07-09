CC = gcc
CFLAGS = -Wall -pthread 
LIBS = -lutil

SRC_DIR = ./src
BIN_DIR = ./bin

SRC_DIR = ./src
BIN_DIR = ./bin


TELNET_SRC_FILES = $(SRC_DIR)/telnet_server.c 
FTP_SRC_FILES = $(SRC_DIR)/ftp_server.c

TELNET_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%.o, $(TELNET_SRC_FILES))
FTP_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%.o, $(FTP_SRC_FILES))

# targets
TELNET_TARGET = $(BIN_DIR)/telnet_server
FTP_TARGET = $(BIN_DIR)/ftp_server

.PHONY: all clean

all: $(TELNET_TARGET) $(FTP_TARGET)
	rm -f $(BIN_DIR)/*.o

$(TELNET_TARGET): $(TELNET_OBJ_FILES)
	$(CC) $(CFLAGS) $(TELNET_OBJ_FILES) -o $@ $(LIBS)

$(FTP_TARGET): $(FTP_OBJ_FILES)
	$(CC) $(CFLAGS) $(FTP_OBJ_FILES) -o $@ $(LIBS)

$(YMODEM_TARGET): $(YMODEM_OBJ_FILES)
	$(CC) $(CFLAGS) $(YMODEM_OBJ_FILES) -o $@ $(LIBS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BIN_DIR)/*

