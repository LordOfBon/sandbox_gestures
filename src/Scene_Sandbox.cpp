#include "Scene_Sandbox.h"
#include "GameEngine.h"
#include "WorldView.hpp"
#include "Assets.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>

#include "imgui.h"
#include "imgui-SFML.h"


Scene_Sandbox::Scene_Sandbox(GameEngine * game)
    : Scene(game)
{
    init();
}

void Scene_Sandbox::init()
{
    ImGui::GetStyle().ScaleAllSizes(2.0f);
    ImGui::GetIO().FontGlobalScale = 2.0f;

    m_view.setWindowSize(Vec2(m_game->window().getSize().x, m_game->window().getSize().y));

    m_view.setView(m_game->window().getView());
        
    m_font = Assets::Instance().getFont("Tech");
    m_text.setFont(m_font);
    m_text.setPosition(10, 5);
    m_text.setCharacterSize(10);

    // initialize rs2
    m_pipe.start();

    // read a sample image and convert it to RGBA needed by sfml
    //m_cvImage = cv::imread("images/seinfeld.jpg", cv::IMREAD_COLOR);
    //cv::cvtColor(m_cvImage, m_cvImage, cv::COLOR_BGR2RGBA);
    //setSprite(m_cvImage);
}

void Scene_Sandbox::captureImage()
{
    // Wait for next set of frames from the camera
    rs2::frameset data = m_pipe.wait_for_frames(); 
    rs2::frame depth = data.get_depth_frame().apply_filter(m_colorMap);
    rs2::frame color = data.get_color_frame();

    // Query frame size (width and height)
    const int dw = depth.as<rs2::video_frame>().get_width();
    const int dh = depth.as<rs2::video_frame>().get_height();
    const int cw = color.as<rs2::video_frame>().get_width();
    const int ch = color.as<rs2::video_frame>().get_height();

    // Create OpenCV matrix of size (w,h) from the colorized depth data
    m_cvDepthImage = cv::Mat(cv::Size(dw, dh), CV_8UC3, (void*)depth.get_data(), cv::Mat::AUTO_STEP);
    cv::cvtColor(m_cvDepthImage, m_cvDepthImage, cv::COLOR_BGR2RGBA);
    m_cvColorImage = cv::Mat(cv::Size(cw, ch), CV_8UC3, (void*)color.get_data(), cv::Mat::AUTO_STEP);
    cv::cvtColor(m_cvColorImage, m_cvColorImage, cv::COLOR_RGB2RGBA);

    // Create the SFML sprites to be rendered
    m_sfDepthImage.create(m_cvDepthImage.cols, m_cvDepthImage.rows, m_cvDepthImage.ptr());
    m_sfDepthTexture.loadFromImage(m_sfDepthImage);
    m_depthSprite.setTexture(m_sfDepthTexture);

    m_sfColorImage.create(m_cvColorImage.cols, m_cvColorImage.rows, m_cvColorImage.ptr());
    m_sfColorTexture.loadFromImage(m_sfColorImage);
    m_colorSprite.setTexture(m_sfColorTexture);
}

void Scene_Sandbox::onFrame()
{
    m_view.update();
    captureImage();
    sUserInput();
    sRender(); 
    renderUI();
    m_currentFrame++;
}

void Scene_Sandbox::sUserInput()
{
    sf::Event event;
    while (m_game->window().pollEvent(event))
    {
        ImGui::SFML::ProcessEvent(m_game->window(), event);

        // this event triggers when the window is closed
        if (event.type == sf::Event::Closed)
        {
            m_game->quit();
        }

        // this event is triggered when a key is pressed
        if (event.type == sf::Event::KeyPressed)
        {
            switch (event.key.code)
            {
                case sf::Keyboard::Escape:
                {
                    m_game->changeScene("MENU", nullptr, true);
                    break;
                }
            }
        }

        if (event.type == sf::Event::KeyReleased)
        {
            switch (event.key.code)
            {
            case sf::Keyboard::W: break;
            case sf::Keyboard::A: break;
            case sf::Keyboard::S: break;
            case sf::Keyboard::D: break;
            }
        }

        if (event.type == sf::Event::MouseButtonPressed)
        {
            // happens when the left mouse button is pressed
            if (event.mouseButton.button == sf::Mouse::Left)
            {
                m_view.stopScroll();
            }

            // happens when the right mouse button is pressed
            if (event.mouseButton.button == sf::Mouse::Right)
            {
                m_drag = { event.mouseButton.x, event.mouseButton.y };
                m_view.stopScroll();
            }
        }

        // happens when the mouse button is released
        if (event.type == sf::Event::MouseButtonReleased)
        {
            // let go of the currently selected rectangle
            if (event.mouseButton.button == sf::Mouse::Left)
            {
                
            }

            // let go of the currently selected rectangle
            if (event.mouseButton.button == sf::Mouse::Right)
            {
                m_drag = { -1, -1 };
            }
        }

        if (event.type == sf::Event::MouseWheelMoved)
        {
            double zoom = 1.0 - (0.2 * event.mouseWheel.delta);
            m_view.zoomTo(zoom, Vec2(event.mouseWheel.x, event.mouseWheel.y));
        }

        // happens whenever the mouse is being moved
        if (event.type == sf::Event::MouseMoved)
        {
            m_mouseScreen = { event.mouseMove.x, event.mouseMove.y };

            // record the current mouse position in universe coordinates
            m_mouseWorld = m_view.windowToWorld(m_mouseScreen);

            if (m_drag.x != -1)
            {
                auto prev = m_view.windowToWorld(m_drag);
                auto curr = m_view.windowToWorld({ event.mouseMove.x, event.mouseMove.y });
                auto scroll = prev - curr;
                m_view.scroll(prev - curr);
                m_drag = { event.mouseMove.x, event.mouseMove.y };
            }
        }
    }
}

// renders the scene
void Scene_Sandbox::sRender()
{
    const sf::Color gridColor(64, 64, 64);

    m_game->window().clear();
    m_lineStrip.clear();
    m_quadArray.clear();
    m_game->window().setView(m_view.getSFMLView());

    m_depthSprite.setColor(sf::Color(255, 255, 255, m_depthAlpha));
    m_depthSprite.setPosition(m_depthPos[0], m_depthPos[1]);
    m_depthSprite.setScale(m_depthScale, m_depthScale);

    m_colorSprite.setColor(sf::Color(255, 255, 255, m_colorAlpha));
    m_colorSprite.setPosition(m_colorPos[0], m_colorPos[1]);
    m_colorSprite.setScale(m_colorScale, m_colorScale);

    if (m_drawDepth) { m_game->window().draw(m_depthSprite); }
    if (m_drawColor) { m_game->window().draw(m_colorSprite); }
    
    m_game->window().draw(m_quadArray);
    m_game->window().draw(m_lineStrip);
    m_game->window().setView(m_game->window().getDefaultView());
    m_game->window().draw(m_text);
}

void Scene_Sandbox::renderUI()
{
    const char vals[7] = { '.', 'G', '@', 'O', 'T', 'S', 'W' };
   
    ImGui::Begin("Options");

    if (ImGui::BeginTabBar("MyTabBar"))
    {
        if (ImGui::BeginTabItem("Image"))
        {
            // PC Display Options
            ImGui::Checkbox("Depth", &m_drawDepth);
            ImGui::SliderInt("DAlpha", &m_depthAlpha, 0, 255);
            ImGui::SliderFloat2("DPos", m_depthPos, -1000, 1000);
            ImGui::SliderFloat("DScale", &m_depthScale, 0, 2);

            ImGui::Checkbox("Color", &m_drawColor);
            ImGui::SliderInt("CAlpha", &m_colorAlpha, 0, 255);
            ImGui::SliderFloat2("CPos", m_colorPos, -1000, 1000);
            ImGui::SliderFloat("CScale", &m_colorScale, 0, 2);

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

bool Scene_Sandbox::isCameraConnected() {
    rs2::context ctx;  // Create a context object, which is used to manage devices
    rs2::device_list devices = ctx.query_devices();  // Get a list of connected RealSense devices
    return devices.size() > 0;  // Return true if at least one device is connected
}