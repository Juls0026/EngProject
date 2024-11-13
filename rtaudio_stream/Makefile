# Makefile to compile audio_sender and audio_receiver
CXX = g++
CXXFLAGS = -lasound

all: sender receiver

sender: sender/audio_sender.cpp
	$(CXX) -o sender/audio_sender sender/audio_sender.cpp $(CXXFLAGS)

receiver: receiver/audio_receiver.cpp
	$(CXX) -o receiver/audio_receiver receiver/audio_receiver.cpp $(CXXFLAGS)

clean:
	rm -f sender/audio_sender receiver/audio_receiver
