#ifndef _FBO_H_
#define _FBO_H_

#include <GL/glew.h>

class FBO
{
public:

	FBO();

	bool	Create(int width, int height, bool depthStencil = false);	// depthStencil: with depth and stencil buffers
	void	Destroy();
	void	BindTexture();
	void	Set();
	void	Disable();
	GLuint	GetFBOID();
	GLuint	GetTextureID();

private:

	void	CreateTexture(int width, int height);

	GLuint m_frameBufferID;
	GLuint m_textureID;
	GLuint m_depthStencilID;
};

#endif