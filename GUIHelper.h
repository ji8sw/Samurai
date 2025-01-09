#pragma once
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"
#include "imgui_stdlib.h"

namespace Samurai::GUI
{
	static GLFWwindow* window = nullptr;

	// Creates a window and sets up GLFW and OpenGL3
	bool initialize(bool server = false)
	{
		// GLFW
		if (!glfwInit()) 
			return false;

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);


		window = glfwCreateWindow(800, 600, server ? "Samurai - Matchmaker" : "Samurai - Client", nullptr, nullptr);
		if (!window)
		{
			glfwTerminate();
			return false;
		}

		glfwMakeContextCurrent(window);
		glfwSwapInterval(1); // vsync

		// OpenGL3
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		(void)io;

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 330");
		ImGui::StyleColorsDark();

		return true;
	}
	
	// uninitializes glfw, opengl, and imgui
	void cleanup()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	// handles standard things like creating a new imgui frame
	// returns false if the frame shouldnt be drawn due to glfwWindowShouldClose
	// will handle cleanup if glfwWindowShouldClose is true
	bool standardFrameStart()
	{
		if (glfwWindowShouldClose(Samurai::GUI::window)) { cleanup(); return false; }
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		return true;
	}

	// handles standard things like rendering and swapping the buffers, params are the glClearColor params
	void standardFrameEnd(GLclampf r = 0.1f, GLclampf g = 0.1f, GLclampf b = 0.1f, GLclampf a = 0.1f)
	{
		ImGui::Render();
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}
}