// https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg_player.c
// https://katyscode.wordpress.com/2013/02/28/cutting-your-teeth-on-fmod-part-5-real-time-streaming-of-programmatically-generated-audio/
#define PL_MPEG_IMPLEMENTATION
#include "VideoPlayer.hpp"
#include <math.h>

#ifndef GEODE_MOD_ID
#define GEODE_MOD_ID "VideoPlayer"
#endif
#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

#define APP_SHADER_SOURCE(...) #__VA_ARGS__

const char* APP_VERTEX_SHADER = APP_SHADER_SOURCE(
    attribute vec4 a_position;
    attribute vec2 a_texCoord;
    varying vec2 tex_coord;

	void main() {
        tex_coord = a_texCoord;
		gl_Position = CC_MVPMatrix * a_position;
	}
);

const char* APP_FRAGMENT_SHADER_YCRCB = APP_SHADER_SOURCE(
	uniform sampler2D texture_y;
	uniform sampler2D texture_cb;
	uniform sampler2D texture_cr;
    varying vec2 tex_coord;

	mat4 rec601 = mat4(
		1.16438,  0.00000,  1.59603, -0.87079,
		1.16438, -0.39176, -0.81297,  0.52959,
		1.16438,  2.01723,  0.00000, -1.08139,
		0, 0, 0, 1
	);

	void main() {
		float y = texture2D(texture_y, tex_coord).r;
		float cb = texture2D(texture_cb, tex_coord).r;
		float cr = texture2D(texture_cr, tex_coord).r;

		gl_FragColor = vec4(y, cb, cr, 1.0) * rec601;
	}
);

inline void checkpoint(const char* text) {
#ifdef _VIDEO_PLAYER_DEBUG
   std::cout << "[VideoPlayer] Checkpoint: " << text << std::endl;
#endif
}

namespace videoplayer {
    bool VideoPlayer::init(std::filesystem::path const& path, bool loop) {
        if (!CCNode::init()) return false;

        checkpoint("VideoPlayer::init");
        // GENERAL
        m_path = path;
        m_stream = plm_create_with_filename(m_path.string().c_str());
        
        if (!m_stream) {
            std::cerr << "[VideoPlayer] File at " << m_path.string() << " not found." << std::endl;
            // log::error("File at " + m_path.string() + " not found.");
            return false;
        };
        checkpoint("Stream created");

        plm_set_loop(m_stream, loop);
        m_loop = loop;
        checkpoint("Loop set");
        plm_set_video_decode_callback(m_stream, VideoPlayer::videoCallback, this);
        plm_set_audio_decode_callback(m_stream, VideoPlayer::audioCallback, this);
        checkpoint("Callbacks set");

        // VIDEO
        m_dimensions = CCSize(m_stream->video_decoder->mb_width, m_stream->video_decoder->mb_height);
        checkpoint("Dimensions set");
        CCGLProgram* shader = new CCGLProgram;
        checkpoint("Shader created");
        setContentSize(m_dimensions * 4);
        shader->initWithVertexShaderByteArray(APP_VERTEX_SHADER, APP_FRAGMENT_SHADER_YCRCB);
        checkpoint("Shader setup");

        shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        checkpoint("Shader attributes added");

        shader->link();
        checkpoint("Shader linked");
        shader->updateUniforms();
        checkpoint("Shader updated uniforms");

        const char* texture_names[3] = {"texture_y", "texture_cb", "texture_cr"};

        plm_frame_t* frame = &m_stream->video_decoder->frame_current;
        plm_plane_t planes[3] = {frame->y, frame->cb, frame->cr};
        checkpoint("Setting up textures");
        for (int i = 0; i < 3; i++) {
            GLuint texture;
            glGenTextures(1, &texture);
            checkpoint("genTexture");
            glBindTexture(GL_TEXTURE_2D, texture);
            checkpoint("bindTexture");
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            checkpoint("params set");

            glUniform1i(glGetUniformLocation(shader->getProgram(), texture_names[i]), i);
            checkpoint("uniform done");

            m_textures[i] = texture;
        }

        checkpoint("finished planes");
        setShaderProgram(shader);
        checkpoint("set shader program");
        shader->release();
        checkpoint("released shader");
        getShaderProgram()->use();
        checkpoint("using the new shader program");

        // Audio
        initAudio();
        checkpoint("init audio");

        m_paused = false;
        setAnchorPoint({0.5f, 0.5f});
        scheduleUpdate();
        checkpoint("scheduled update");

        return true;
    };

    void VideoPlayer::initAudio() {
        FMODAudioEngine* engine = FMODAudioEngine::sharedEngine();

        int sampleRate = plm_get_samplerate(m_stream);

        FMOD_CREATESOUNDEXINFO soundInfo;
        memset(&soundInfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
        soundInfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
        soundInfo.decodebuffersize = PLM_AUDIO_SAMPLES_PER_FRAME * 2;
        soundInfo.length = sampleRate * 2 * sizeof(float) * plm_get_duration(m_stream);
        soundInfo.numchannels = 2;
        soundInfo.defaultfrequency = sampleRate;
        soundInfo.format = FMOD_SOUND_FORMAT_PCMFLOAT;
        soundInfo.pcmreadcallback = &VideoPlayer::PCMRead;
        soundInfo.userdata = this;

        m_samples = {};
        engine->m_system->createStream(nullptr, FMOD_OPENUSER, &soundInfo, &m_sound);
        
        this->playSound();
        m_channel->setVolume(m_volume);
        
        m_channel->setUserData(this);
        if (m_loop) m_channel->setCallback(&VideoPlayer::audioCallback);
    }

    void VideoPlayer::playSound() {
        bool playing = false;
        if (m_channel != nullptr)
            m_channel->isPlaying(&playing);

        if(!playing) {
            FMODAudioEngine* engine = FMODAudioEngine::sharedEngine();
            engine->m_system->playSound(m_sound, engine->getChannelGroup(0, false), false, &m_channel);
        }
    }

    FMOD_RESULT F_CALLBACK VideoPlayer::audioCallback(FMOD_CHANNELCONTROL *chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void *commandData1, void *commandData2) {
        if (callbackType != FMOD_CHANNELCONTROL_CALLBACK_END) return FMOD_OK;

        VideoPlayer* self;
        ((FMOD::ChannelControl*)chanControl)->getUserData((void**)&self);
        if (self->m_stopped) return FMOD_OK; // For destructor/onExit

        // self->m_channel->stop();
        // self->m_sound->release();
        self->initAudio();
        return FMOD_OK;
    }

    void VideoPlayer::update(float delta) {
        if (!m_paused) {
            this->playSound();
            plm_decode(m_stream, delta);
        }
//        checkpoint("Decoded frame");
    }

    void VideoPlayer::draw() {
        CC_NODE_DRAW_SETUP();
        //        checkpoint("Node draw setup");

        for (int i = 0; i < 3; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        }
        //        checkpoint("Activated and binded textures");

        float w = m_obContentSize.width;
        float h = m_obContentSize.height;

        GLfloat vertices[12] = {
            0, 0,
            w, 0,
            w, h,
            0, 0,
            0, h,
            w, h
        };
        
        GLfloat coordinates[12] = {
            0, 1,
            1, 1,
            1, 0,
            0, 1,
            0, 0,
            1, 0
        };

        GLuint VBO[2];
        glGenBuffers(2, VBO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(coordinates), coordinates, GL_STATIC_DRAW);

        glEnableVertexAttribArray(kCCVertexAttrib_Position);
        glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

        glEnableVertexAttribArray(kCCVertexAttrib_TexCoords);
        glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        //        checkpoint("Vertex attribute pointers set");

        glDrawArrays(GL_TRIANGLES, 0, 6);
        //        checkpoint("Drawing array");

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(2, VBO);
}


    void VideoPlayer::onExit() {
        // ~VideoPlayer();
    }

    VideoPlayer::~VideoPlayer() {
        std::cout << "Releasing video player audio..." << std::endl;
        m_channel->stop();
        m_sound->release();
        m_stopped = true;
    }

    void VideoPlayer::videoCallback(plm_t* mpeg, plm_frame_t* frame, void* user) {
        VideoPlayer* self = (VideoPlayer*) user;

        plm_plane_t* frames[3] = {&frame->y, &frame->cb, &frame->cr};

        for (int i = 0; i < 3; i++) {
            GLuint texture = self->m_textures[i];

            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, texture);

            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_LUMINANCE, frames[i]->width, frames[i]->height, 0,
                GL_LUMINANCE, GL_UNSIGNED_BYTE, frames[i]->data
            );
        }
//        checkpoint("video callback finished");
    }

    void VideoPlayer::audioCallback(plm_t* mpeg, plm_samples_t* samples, void* user) {
        VideoPlayer* self = (VideoPlayer*) user;
        self->m_channel->setPaused(false);
        
        for (unsigned int i = 0; i < samples->count * 2; i++) {
            self->m_samples.push(samples->interleaved[i]);
        }

        while (self->m_samples.size() > PLM_AUDIO_SAMPLES_PER_FRAME * 16) { // i think this is 4 frames of wiggle room but im not sure it just sounds best this way
            self->m_samples.pop();
        }
    }

    FMOD_RESULT F_CALLBACK VideoPlayer::PCMRead(FMOD_SOUND *sound, void *data, unsigned int length) {
        VideoPlayer* self;
        ((FMOD::Sound*)sound)->getUserData((void**)&self);
        if (!self) return FMOD_OK;

        float* buf = (float*)data;

        for (unsigned int i = 0; i < (length / sizeof(float)) / 2 && self->m_samples.size() >= 2; i++) { // Always keep the ears synced
            buf[2*i]=self->m_samples.front();
            self->m_samples.pop();

            buf[2*i+1]=self->m_samples.front();
            self->m_samples.pop();
        }

        return FMOD_OK;
    }


    VideoPlayer* VideoPlayer::create(std::filesystem::path const& path, bool loop) {
        VideoPlayer* ret = new VideoPlayer;
        if (ret && ret->init(path, loop)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    };

    void VideoPlayer::setHeight(float height) {
        setContentSize({height * m_dimensions.aspect(), height});
    }

    void VideoPlayer::setWidth(float width) {
        setContentSize({width, width / m_dimensions.aspect()});
    }

    void VideoPlayer::fillSize(CCSize size) {
        if (m_dimensions.aspect() > size.aspect()) {
            setWidth(size.width);
        } else {
            setHeight(size.height);
        }
    }

    void VideoPlayer::setVolume(float volume) {
        m_volume = volume;
        m_channel->setVolume(volume);
    }

    void VideoPlayer::pause() {
        m_channel->setPaused(true);
        m_paused = true;
    }

    void VideoPlayer::resume() {
        m_channel->setPaused(false);
        m_paused = false;
    }

    void VideoPlayer::toggle() {
        if (m_paused) return resume();
        pause();
    }

    bool VideoPlayer::isPaused() {
        return m_paused;
    }
}

#undef GEODE_MOD_ID
