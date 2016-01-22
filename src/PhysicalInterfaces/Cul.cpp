/* Copyright 2013-2015 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "../BidCoSPacket.h"
#include "homegear-base/BaseLib.h"
#include "../GD.h"
#include "Cul.h"

namespace BidCoS
{

Cul::Cul(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IBidCoSInterface(settings), BaseLib::ITimedQueue(GD::bl)
{
	_bl = GD::bl;
	_out.init(GD::bl);
	_out.setPrefix(GD::out.getPrefix() + "CUL \"" + settings->id + "\": ");

	if(settings->listenThreadPriority == -1)
	{
		settings->listenThreadPriority = 45;
		settings->listenThreadPolicy = SCHED_FIFO;
	}

	_aesHandshake.reset(new AesHandshake(_bl, _out, _myAddress, _rfKey, _oldRfKey, _currentRfKeyIndex));
}

Cul::~Cul()
{
	try
	{
		_stopCallbackThread = true;
		GD::bl->threadManager.join(_listenThread);
		closeDevice();
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::addPeer(PeerInfo peerInfo)
{
	try
	{
		if(peerInfo.address == 0) return;
		_peersMutex.lock();
		//Remove old peer first. removePeer() is not called, so we don't need to unlock _peersMutex
		if(_peers.find(peerInfo.address) != _peers.end()) _peers.erase(peerInfo.address);
		_peers[peerInfo.address] = peerInfo;
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
}

void Cul::addPeers(std::vector<PeerInfo>& peerInfos)
{
	try
	{
		for(std::vector<PeerInfo>::iterator i = peerInfos.begin(); i != peerInfos.end(); ++i)
		{
			addPeer(*i);
		}
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::removePeer(int32_t address)
{
	try
	{
		_peersMutex.lock();
		if(_peers.find(address) != _peers.end()) _peers.erase(address);
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
}

void Cul::processQueueEntry(int32_t index, int64_t id, std::shared_ptr<BaseLib::ITimedQueueEntry>& entry)
{
	try
	{
		std::shared_ptr<QueueEntry> queueEntry;
		queueEntry = std::dynamic_pointer_cast<QueueEntry>(entry);
		if(!queueEntry || !queueEntry->packet) return;
		writeToDevice("As" + queueEntry->packet->hexString() + "\n", true);
		queueEntry->packet->setTimeSending(BaseLib::HelperFunctions::getTime());

		// {{{ Remove packet from queue id map
			std::lock_guard<std::mutex> idGuard(_queueIdsMutex);
			std::map<int32_t, std::set<int64_t>>::iterator idIterator = _queueIds.find(queueEntry->packet->destinationAddress());
			if(idIterator == _queueIds.end()) return;
			idIterator->second.erase(id);
			if(idIterator->second.empty()) _queueIds.erase(idIterator);
		// }}}
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void Cul::queuePacket(std::shared_ptr<BidCoSPacket> packet, int64_t sendingTime)
{
	try
	{
		if(sendingTime == 0)
		{
			sendingTime = packet->timeReceived();
			if(sendingTime <= 0) sendingTime = BaseLib::HelperFunctions::getTime();
			sendingTime = sendingTime + _settings->responseDelay;
		}
		std::shared_ptr<BaseLib::ITimedQueueEntry> entry(new QueueEntry(sendingTime, packet));
		int64_t id;
		if(!enqueue(0, entry, id)) _out.printError("Error: Too many packets are queued to be processed. Your packet processing is too slow. Dropping packet.");

		// {{{ Add packet to queue id map
			std::lock_guard<std::mutex> idGuard(_queueIdsMutex);
			_queueIds[packet->destinationAddress()].insert(id);
		// }}}
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(!packet)
		{
			_out.printWarning("Warning: Packet was nullptr.");
			return;
		}
		if(_fileDescriptor->descriptor == -1) throw(BaseLib::Exception("Couldn't write to CUL device, because the file descriptor is not valid: " + _settings->device));
		if(packet->payload()->size() > 54)
		{
			if(_bl->debugLevel >= 2) _out.printError("Error: Tried to send packet larger than 64 bytes. That is not supported.");
			return;
		}
		std::shared_ptr<BidCoSPacket> bidCoSPacket(std::dynamic_pointer_cast<BidCoSPacket>(packet));
		if(!bidCoSPacket) return;
		if(_updateMode && !bidCoSPacket->isUpdatePacket())
		{
			_out.printInfo("Info: Can't send packet to BidCoS peer with address 0x" + BaseLib::HelperFunctions::getHexString(packet->destinationAddress(), 6) + ", because update mode is enabled.");
			return;
		}
		if(bidCoSPacket->messageType() == 0x02 && packet->senderAddress() == _myAddress && bidCoSPacket->controlByte() == 0x80 && bidCoSPacket->payload()->size() == 1 && bidCoSPacket->payload()->at(0) == 0)
		{
			_out.printDebug("Debug: Ignoring ACK packet.", 6);
			_lastPacketSent = BaseLib::HelperFunctions::getTime();
			return;
		}
		if((bidCoSPacket->controlByte() & 0x01) && packet->senderAddress() == _myAddress && (bidCoSPacket->payload()->empty() || (bidCoSPacket->payload()->size() == 1 && bidCoSPacket->payload()->at(0) == 0)))
		{
			_out.printDebug("Debug: Ignoring wake up packet.", 6);
			_lastPacketSent = BaseLib::HelperFunctions::getTime();
			return;
		}
		if(bidCoSPacket->messageType() == 0x04 && bidCoSPacket->payload()->size() == 2 && bidCoSPacket->payload()->at(0) == 1) //Set new AES key
		{
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			std::map<int32_t, PeerInfo>::iterator peerIterator = _peers.find(bidCoSPacket->destinationAddress());
			if(peerIterator != _peers.end())
			{
				if((bidCoSPacket->payload()->at(1) + 2) / 2 > peerIterator->second.keyIndex)
				{
					if(!_aesHandshake->generateKeyChangePacket(bidCoSPacket)) return;
				}
				else
				{
					_out.printInfo("Info: Ignoring AES key update packet, because a key with this index is already set.");
					std::vector<uint8_t> payload { 0 };
					std::shared_ptr<BidCoSPacket> ackPacket(new BidCoSPacket(bidCoSPacket->messageCounter(), 0x80, 0x02, bidCoSPacket->destinationAddress(), _myAddress, payload));
					raisePacketReceived(ackPacket);
					return;
				}
			}

		}

		writeToDevice("As" + packet->hexString() + "\n", true);
		packet->setTimeSending(BaseLib::HelperFunctions::getTime());
		_aesHandshake->setMFrame(bidCoSPacket);
		queuePacket(bidCoSPacket, packet->timeSending() + 200);
		queuePacket(bidCoSPacket, packet->timeSending() + 400);
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::enableUpdateMode()
{
	try
	{
		_updateMode = true;
		writeToDevice("AR\n", false);
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::disableUpdateMode()
{
	try
	{
		stopListening();
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		startListening();
		_updateMode = false;
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::openDevice()
{
	try
	{
		if(_fileDescriptor->descriptor > -1) closeDevice();

		_lockfile = "/var/lock" + _settings->device.substr(_settings->device.find_last_of('/')) + ".lock";
		int lockfileDescriptor = open(_lockfile.c_str(), O_WRONLY | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if(lockfileDescriptor == -1)
		{
			if(errno != EEXIST)
			{
				_out.printCritical("Couldn't create lockfile " + _lockfile + ": " + strerror(errno));
				return;
			}

			int processID = 0;
			std::ifstream lockfileStream(_lockfile.c_str());
			lockfileStream >> processID;
			if(getpid() != processID && kill(processID, 0) == 0)
			{
				_out.printCritical("CUL device is in use: " + _settings->device);
				return;
			}
			unlink(_lockfile.c_str());
			lockfileDescriptor = open(_lockfile.c_str(), O_WRONLY | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
			if(lockfileDescriptor == -1)
			{
				_out.printCritical("Couldn't create lockfile " + _lockfile + ": " + strerror(errno));
				return;
			}
		}
		dprintf(lockfileDescriptor, "%10i", getpid());
		close(lockfileDescriptor);
		//std::string chmod("chmod 666 " + _lockfile);
		//system(chmod.c_str());

		_fileDescriptor = _bl->fileDescriptorManager.add(open(_settings->device.c_str(), O_RDWR | O_NOCTTY));
		if(_fileDescriptor->descriptor == -1)
		{
			_out.printCritical("Couldn't open CUL device \"" + _settings->device + "\": " + strerror(errno));
			return;
		}

		setupDevice();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::closeDevice()
{
	try
	{
		_bl->fileDescriptorManager.close(_fileDescriptor);
		unlink(_lockfile.c_str());
	}
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::setupDevice()
{
	try
	{
		if(_fileDescriptor->descriptor == -1) return;
		memset(&_termios, 0, sizeof(termios));

		_termios.c_cflag = B38400 | CS8 | CREAD;
		_termios.c_iflag = 0;
		_termios.c_oflag = 0;
		_termios.c_lflag = 0;
		_termios.c_cc[VMIN] = 1;
		_termios.c_cc[VTIME] = 0;

		cfsetispeed(&_termios, B38400);
		cfsetospeed(&_termios, B38400);

		if(tcflush(_fileDescriptor->descriptor, TCIFLUSH) == -1) throw(BaseLib::Exception("Couldn't flush CUL device " + _settings->device));
		if(tcsetattr(_fileDescriptor->descriptor, TCSANOW, &_termios) == -1) throw(BaseLib::Exception("Couldn't set CUL device settings: " + _settings->device));

		std::this_thread::sleep_for(std::chrono::milliseconds(2000));

		int flags = fcntl(_fileDescriptor->descriptor, F_GETFL);
		if(!(flags & O_NONBLOCK))
		{
			if(fcntl(_fileDescriptor->descriptor, F_SETFL, flags | O_NONBLOCK) == -1)
			{
				throw(BaseLib::Exception("Couldn't set CUL device to non blocking mode: " + _settings->device));
			}
		}
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::string Cul::readFromDevice()
{
	try
	{
		if(_stopped) return "";
		if(_fileDescriptor->descriptor == -1)
		{
			_out.printCritical("Couldn't read from CUL device, because the file descriptor is not valid: " + _settings->device + ". Trying to reopen...");
			closeDevice();
			std::this_thread::sleep_for(std::chrono::milliseconds(5000));
			openDevice();
			if(!isOpen()) return "";
			writeToDevice("X21\nAr\n", false);
		}
		std::string packet;
		int32_t i;
		char localBuffer[1] = "";
		fd_set readFileDescriptor;
		FD_ZERO(&readFileDescriptor);
		FD_SET(_fileDescriptor->descriptor, &readFileDescriptor);

		while((!_stopCallbackThread && localBuffer[0] != '\n' && _fileDescriptor->descriptor > -1))
		{
			FD_ZERO(&readFileDescriptor);
			FD_SET(_fileDescriptor->descriptor, &readFileDescriptor);
			//Timeout needs to be set every time, so don't put it outside of the while loop
			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 500000;
			i = select(_fileDescriptor->descriptor + 1, &readFileDescriptor, NULL, NULL, &timeout);
			switch(i)
			{
				case 0: //Timeout
					if(!_stopCallbackThread) continue;
					else return "";
				case -1:
					_out.printError("Error reading from CUL device: " + _settings->device);
					return "";
				case 1:
					break;
				default:
					_out.printError("Error reading from CUL device: " + _settings->device);
					return "";
			}

			i = read(_fileDescriptor->descriptor, localBuffer, 1);
			if(i == -1)
			{
				if(errno == EAGAIN) continue;
				_out.printError("Error reading from CUL device: " + _settings->device);
				return "";
			}
			packet.push_back(localBuffer[0]);
			if(packet.size() > 200)
			{
				_out.printError("CUL was disconnected.");
				closeDevice();
				return "";
			}
		}
		return packet;
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
	return "";
}

void Cul::writeToDevice(std::string data, bool printSending)
{
    try
    {
    	if(_stopped) return;
        if(_fileDescriptor->descriptor == -1) throw(BaseLib::Exception("Couldn't write to CUL device, because the file descriptor is not valid: " + _settings->device));
        int32_t bytesWritten = 0;
        int32_t i;
        if(_bl->debugLevel > 3 && printSending)
        {
            _out.printInfo("Info: Sending (" + _settings->id + "): " + data.substr(2, data.size() - 3));
        }
        _sendMutex.lock();
        while(bytesWritten < (signed)data.length())
        {
            i = write(_fileDescriptor->descriptor, data.c_str() + bytesWritten, data.length() - bytesWritten);
            if(i == -1)
            {
                if(errno == EAGAIN) continue;
                throw(BaseLib::Exception("Error writing to CUL device (3, " + std::to_string(errno) + "): " + _settings->device));
            }
            bytesWritten += i;
        }
    }
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _sendMutex.unlock();
    _lastPacketSent = BaseLib::HelperFunctions::getTime();
}

void Cul::startListening()
{
	try
	{
		stopListening();

		if(!_aesHandshake) return; //AES is not initialized

		if(!GD::family->getCentral())
		{
			_stopCallbackThread = true;
			_out.printError("Error: Could not get central address. Stopping listening.");
			return;
		}
		_myAddress = GD::family->getCentral()->getAddress();
		_aesHandshake->setMyAddress(_myAddress);

		startQueue(0, 45, SCHED_FIFO);
		openDevice();
		if(_fileDescriptor->descriptor == -1) return;
		_stopped = false;
		writeToDevice("X21\nAr\n", false);
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		if(_settings->listenThreadPriority > -1) GD::bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Cul::listen, this);
		else GD::bl->threadManager.start(_listenThread, true, &Cul::listen, this);
		IPhysicalInterface::startListening();
	}
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::stopListening()
{
	try
	{
		stopQueue(0);
		_stopCallbackThread = true;
		GD::bl->threadManager.join(_listenThread);
		_stopCallbackThread = false;
		if(_fileDescriptor->descriptor > -1)
		{
			//Other X commands than 00 seem to slow down data processing
			writeToDevice("Ax\nX00\n", false);
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			closeDevice();
		}
		_stopped = true;
		IPhysicalInterface::stopListening();
	}
	catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::listen()
{
    try
    {
        while(!_stopCallbackThread)
        {
        	if(_stopped)
        	{
        		std::this_thread::sleep_for(std::chrono::milliseconds(200));
        		if(_stopCallbackThread) return;
        		continue;
        	}
        	std::string packetHex = readFromDevice();
        	if(packetHex.size() > 21) //21 is minimal packet length (=10 bytes + CUL "A")
        	{
				std::shared_ptr<BidCoSPacket> packet(new BidCoSPacket(packetHex, BaseLib::HelperFunctions::getTime()));
				if(packet->destinationAddress() == _myAddress)
				{
					bool aesHandshake = false;
					bool wakeUp = false;
					bool knowsPeer = false;
					try
					{
						std::lock_guard<std::mutex> peersGuard(_peersMutex);
						std::map<int32_t, PeerInfo>::iterator peerIterator = _peers.find(packet->senderAddress());
						if(peerIterator != _peers.end())
						{
							knowsPeer = true;
							wakeUp = peerIterator->second.wakeUp;
							if(packet->messageType() == 0x03)
							{
								std::shared_ptr<BidCoSPacket> mFrame;
								std::shared_ptr<BidCoSPacket> aFrame = _aesHandshake->getAFrame(packet, mFrame, peerIterator->second.keyIndex, wakeUp);
								if(!aFrame)
								{
									if(mFrame) _out.printError("Error: AES handshake failed for packet: " + mFrame->hexString());
									else _out.printError("Error: No m-Frame found for r-Frame.");
									continue;
								}
								if(_bl->debugLevel >= 5) _out.printDebug("Debug: AES handshake successful.");
								queuePacket(aFrame);
								mFrame->setTimeReceived(BaseLib::HelperFunctions::getTime());
								raisePacketReceived(mFrame);

								continue;
							}
							else if(packet->messageType() == 0x02 && packet->payload()->size() == 8 && packet->payload()->at(0) == 0x04)
							{
								peerIterator->second.keyIndex = packet->payload()->back() / 2;
								std::shared_ptr<BidCoSPacket> mFrame;
								std::shared_ptr<BidCoSPacket> rFrame = _aesHandshake->getRFrame(packet, mFrame, peerIterator->second.keyIndex);
								if(!rFrame)
								{
									if(mFrame) _out.printError("Error: AES handshake failed for packet: " + mFrame->hexString());
									else _out.printError("Error: No m-Frame found for c-Frame.");
									continue;
								}

								// {{{ Remove wrongly queued non AES packets from queue id map
									bool requeue = false;
									{
										std::lock_guard<std::mutex> idGuard(_queueIdsMutex);
										std::map<int32_t, std::set<int64_t>>::iterator idIterator = _queueIds.find(packet->senderAddress());

										if(idIterator != _queueIds.end() && *(idIterator->second.begin()) < mFrame->timeSending() + 300)
										{
											requeue = true;
											for(std::set<int64_t>::iterator queueId = idIterator->second.begin(); queueId != idIterator->second.end(); ++queueId)
											{
												removeQueueEntry(0, *queueId);
											}
											_queueIds.erase(idIterator);
										}
									}
									if(requeue)
									{
										queuePacket(mFrame, mFrame->timeSending() + 600);
										queuePacket(mFrame, mFrame->timeSending() + 1200);
									}
								// }}}

								queuePacket(rFrame);
								continue;
							}
							else if(packet->messageType() == 0x02)
							{
								if(_aesHandshake->handshakeStarted(packet->senderAddress()) && !_aesHandshake->checkAFrame(packet))
								{
									_out.printError("Error: ACK has invalid signature.");
									continue;
								}

								// {{{ Remove packet from queue id map
								{
									std::lock_guard<std::mutex> idGuard(_queueIdsMutex);
									std::map<int32_t, std::set<int64_t>>::iterator idIterator = _queueIds.find(packet->senderAddress());
									if(idIterator != _queueIds.end())
									{
										for(std::set<int64_t>::iterator queueId = idIterator->second.begin(); queueId != idIterator->second.end(); ++queueId)
										{
											removeQueueEntry(0, *queueId);
										}
										_queueIds.erase(idIterator);
									}
								}
								// }}}
							}
							else
							{
								// {{{ Remove packet from queue id map
								{
									std::lock_guard<std::mutex> idGuard(_queueIdsMutex);
									std::map<int32_t, std::set<int64_t>>::iterator idIterator = _queueIds.find(packet->senderAddress());
									if(idIterator != _queueIds.end())
									{
										for(std::set<int64_t>::iterator queueId = idIterator->second.begin(); queueId != idIterator->second.end(); ++queueId)
										{
											removeQueueEntry(0, *queueId);
										}
										_queueIds.erase(idIterator);
									}
								}
								// }}}

								if(packet->payload()->size() > 1)
								{
									//Packet type 0x4X has channel at index 0 all other types at index 1
									if((packet->messageType() & 0xF0) == 0x40 && peerIterator->second.aesChannels[packet->payload()->at(0) & 0x3F]) aesHandshake = true;
									else if(peerIterator->second.aesChannels[packet->payload()->at(1) & 0x3F]) aesHandshake = true;
								}
								else if(packet->payload()->size() == 1 && (packet->messageType() & 0xF0) == 0x40 && peerIterator->second.aesChannels[packet->payload()->at(0) & 0x3F]) aesHandshake = true;
								else if(peerIterator->second.aesChannels[0]) aesHandshake = true;
							}
						}
					}
					catch(const std::exception& ex)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(BaseLib::Exception& ex)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(...)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
					}
					if(aesHandshake)
					{
						if(_bl->debugLevel >= 5) _out.printDebug("Debug: Doing AES handshake.");
						queuePacket(_aesHandshake->getCFrame(packet));
					}
					else
					{
						if(knowsPeer && (packet->controlByte() & 0x20))
						{
							std::vector<uint8_t> payload { 0 };
							uint8_t controlByte = 0x80;
							if((packet->controlByte() & 2) && wakeUp && packet->messageType() != 0) controlByte |= 1;
							std::shared_ptr<BidCoSPacket> ackPacket(new BidCoSPacket(packet->messageCounter(), controlByte, 0x02, _myAddress, packet->senderAddress(), payload));
							queuePacket(ackPacket);
						}
						raisePacketReceived(packet);
					}
				}
				else if(packet->destinationAddress() == 0 && (packet->controlByte() & 2)) //Packet is wake me up packet
				{
					try
					{
						std::lock_guard<std::mutex> peersGuard(_peersMutex);
						std::map<int32_t, PeerInfo>::iterator peerIterator = _peers.find(packet->senderAddress());
						if(peerIterator != _peers.end() && peerIterator->second.wakeUp)
						{
							std::vector<uint8_t> payload;
							std::shared_ptr<BidCoSPacket> wakeUpPacket(new BidCoSPacket(packet->messageCounter(), 0xA1, 0x12, _myAddress, packet->senderAddress(), payload));
							queuePacket(wakeUpPacket);
						}
						raisePacketReceived(packet);
					}
					catch(const std::exception& ex)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(BaseLib::Exception& ex)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(...)
					{
						_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
					}
				}
				else raisePacketReceived(packet);
				if(_bl->hf.getTime() - _lastAesHandshakeGc > 30000)
				{
					_lastAesHandshakeGc = _bl->hf.getTime();
					_aesHandshake->collectGarbage();
				}
        	}
        	else if(!packetHex.empty())
        	{
        		if(packetHex == "LOVF\n") _out.printWarning("Warning: CUL with id " + _settings->id + " reached 1% limit. You need to wait, before sending is allowed again.");
        		else if(packetHex == "A") continue;
        		else _out.printWarning("Warning: Too short packet received: " + packetHex);
        	}
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Cul::setup(int32_t userID, int32_t groupID)
{
    try
    {
    	setDevicePermission(userID, groupID);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

}
