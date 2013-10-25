/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef SHARED_MEMORY_FACTORY_H
#define SHARED_MEMORY_FACTORY_H

#include "../Util/OSRMException.h"
#include "../Util/SimpleLogger.h"

#include <boost/noncopyable.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/xsi_shared_memory.hpp>

#include <algorithm>
#include <exception>

struct OSRMLockFile {
	boost::filesystem::path operator()() {
		boost::filesystem::path temp_dir =
			boost::filesystem::temp_directory_path();
	    boost::filesystem::path lock_file = temp_dir / "osrm.lock";
	    return lock_file;
	}
};

class SharedMemory : boost::noncopyable {

	//Remove shared memory on destruction
	class shm_remove : boost::noncopyable {
	private:
		int m_shmid;
		bool m_initialized;
	public:
		void SetID(int shmid) {
		 	m_shmid = shmid;
		 	m_initialized = true;
		}

		shm_remove() : m_shmid(INT_MIN), m_initialized(false) {}

		~shm_remove(){
			if(m_initialized) {
				SimpleLogger().Write(logDEBUG) <<
					"automatic memory deallocation";
				boost::interprocess::xsi_shared_memory::remove(m_shmid);
			}
		}
	};

public:
	void * Ptr() const {
		return region.get_address();
	}

	template<typename IdentifierT >
	SharedMemory(
		const boost::filesystem::path & lock_file,
		const IdentifierT id,
		const unsigned size = 0,
		bool read_write = false,
		bool remove_prev = true
	) : key(
			lock_file.string().c_str(),
			id
	) {
    	if( 0 == size ){ //read_only
    		shm = boost::interprocess::xsi_shared_memory (
    			boost::interprocess::open_only,
    			key
			);
    		region = boost::interprocess::mapped_region (
    			shm,
    			(
    				read_write ?
    					boost::interprocess::read_write :
    					boost::interprocess::read_only
    			)
			);
    	} else { //writeable pointer
    		//remove previously allocated mem
    		if( remove_prev ) {
	    		Remove(key);
	    	}
    		shm = boost::interprocess::xsi_shared_memory (
    			boost::interprocess::open_or_create,
    			key,
    			size
    		);
		    region = boost::interprocess::mapped_region (
		    	shm,
		    	boost::interprocess::read_write
	    	);

 			remover.SetID( shm.get_shmid() );
 			SimpleLogger().Write(logDEBUG) <<
 				"writeable memory allocated " << size << " bytes";
    	}
	}

	// void Swap(SharedMemory & other) {
	// 	SimpleLogger().Write() << "&prev: " << shm.get_shmid();
	// 	shm.swap(other.shm);
	// 	region.swap(other.region);
	// 	boost::interprocess::xsi_key temp_key = other.key;
	// 	other.key = key;
	// 	key = temp_key;
	// 	SimpleLogger().Write() << "&after: " << shm.get_shmid();
	// }

	void Swap(SharedMemory * other) {
		SimpleLogger().Write() << "this key: " << key.get_key() << ", other: " << other->key.get_key();
		SimpleLogger().Write() << "this id: " << shm.get_shmid() << ", other: " << other->shm.get_shmid();
		shm.swap(other->shm);
		region.swap(other->region);
		boost::interprocess::xsi_key temp_key = other->key;
		other->key = key;
		key = temp_key;
		SimpleLogger().Write() << "swap done";
		SimpleLogger().Write() << "this key: " << key.get_key() << ", other: " << other->key.get_key();
		SimpleLogger().Write() << "this id: " << shm.get_shmid() << ", other: " << other->shm.get_shmid();

	}

	template<typename IdentifierT >
	static bool RegionExists(
		const IdentifierT id
	) {
		bool result = true;
		try {
			OSRMLockFile lock_file;
			boost::interprocess::xsi_key key( lock_file().string().c_str(), id );
			result = RegionExists(key);
		} catch(...) {
			result = false;
		}
		return result;
	}

	template<typename IdentifierT >
	static void Remove(
		const IdentifierT id
	) {
		OSRMLockFile lock_file;
		boost::interprocess::xsi_key key( lock_file().string().c_str(), id );
		Remove(key);
	}

private:
	static bool RegionExists( const boost::interprocess::xsi_key &key ) {
		bool result = true;
	    try {
		    boost::interprocess::xsi_shared_memory shm(
		        boost::interprocess::open_only,
		        key
		    );
	    } catch(...) {
	    	result = false;
	    }
	    return result;
	}

	static void Remove(
		const boost::interprocess::xsi_key &key
	) {
		try{
			SimpleLogger().Write(logDEBUG) << "deallocating prev memory";
			boost::interprocess::xsi_shared_memory xsi(
				boost::interprocess::open_only,
				key
			);
			boost::interprocess::xsi_shared_memory::remove(xsi.get_shmid());
		} catch(boost::interprocess::interprocess_exception &e){
			if(e.get_error_code() != boost::interprocess::not_found_error) {
				throw;
			}
		}
	}

	boost::interprocess::xsi_key key;
	boost::interprocess::xsi_shared_memory shm;
	boost::interprocess::mapped_region region;
	shm_remove remover;
};

template<class LockFileT = OSRMLockFile>
class SharedMemoryFactory_tmpl : boost::noncopyable {
public:

	template<typename IdentifierT >
	static void Swap(const IdentifierT & id1, const IdentifierT & id2) {
		SharedMemory * memory_1 = Get(id1);
		SharedMemory * memory_2 = Get(id2);

		memory_1->Swap(memory_2);
	}

	template<typename IdentifierT >
	static SharedMemory * Get(
		const IdentifierT & id,
		const unsigned size = 0,
		bool read_write = false,
		bool remove_prev = true
	) {
		try {
			LockFileT lock_file;
		    if(!boost::filesystem::exists(lock_file()) ) {
		    	if( 0 == size ) {
	      			throw OSRMException("lock file does not exist, exiting");
	      		} else {
	      			boost::filesystem::ofstream ofs(lock_file());
	      			ofs.close();
	      		}
	      	}
			return new SharedMemory(lock_file(), id, size, read_write, remove_prev);
	   	} catch(const boost::interprocess::interprocess_exception &e){
    		SimpleLogger().Write(logWARNING) <<
    			"caught exception: " << e.what() <<
    			", code " << e.get_error_code();
    		throw OSRMException(e.what());
    	}
	}

private:
	SharedMemoryFactory_tmpl() {}
};

typedef SharedMemoryFactory_tmpl<> SharedMemoryFactory;

#endif /* SHARED_MEMORY_POINTER_FACTORY_H */
