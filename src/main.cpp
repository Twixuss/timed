#include "../dep/tl/include/tl/profiler.h"
#include "../dep/tl/include/tl/common.h"
#include "../dep/tl/include/tl/imgui_opengl.h"
#include "../dep/tl/include/tl/window.h"
#include "../dep/tl/include/tl/opengl.h"
#include "../dep/tl/include/tl/font.h"
#include "../dep/tl/include/tl/shader_catalog_opengl.h"
#include "../dep/tl/include/tl/math_random.h"

using namespace TL;
using namespace TL::OpenGL;

#include <unordered_map>
#include <unordered_set>
#include <algorithm>

struct FontVertex {
	v2f position;
	v2f uv;
};

struct TextCache {
	List<utf8> text;
	List<FontVertex> vertices;
	GLuint buffer;
};

std::unordered_map<u32, TextCache> text_caches;

ShaderCatalog shader_catalog;

ShaderCatalog::Entry &operator""shader(char const *string, umm size) {
	return *find(shader_catalog, Span((utf8 *)string, size));
}

Window *window;
FontCollection *font_collection;

struct TimePoint {
	s64 nanoseconds;
};

TimePoint nanoseconds(s64 value) {
	return {value};
}

void append(StringBuilder &b, TimePoint p) {
	auto unit = "ns"s;
	f64 value = p.nanoseconds;
	if (value >= 1000) { value /= 1000; unit = "us"s; }
	if (value >= 1000) { value /= 1000; unit = "ms"s; }
	if (value >= 1000) { value /= 1000; unit = "s"s; }
	append(b, FormatFloat(value, 1));
	append(b, unit);
}

u32 get_hash(Span<utf8> str) {
	u32 hash = 0x0F1E2D3C4B5A6978;
	u32 index = 0;
	for (auto c : str) {
		hash ^= (u32)c << (index & ((sizeof(u32) - 1) * 8));
	}
	return hash;
}

s32 tl_main(Span<Span<utf8>> arguments) {
	init_allocator();
	defer { deinit_allocator(); };

	Profiler::init();
	defer { Profiler::deinit(); };

	show_console_window();
	current_printer = console_printer;

	Span<filechar> font_paths[] = {
		TL_FILE_STRING("../data/fonts/DejaVuSans.ttf\0"s),
	};
	font_collection = create_font_collection(font_paths);
	font_collection->update_atlas = [](umm texture, void *data, v2u size) {
		if (!texture) {
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		} else {
			glBindTexture(GL_TEXTURE_2D, texture);
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size.x, size.y, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		return texture;
	};

	CreateWindowInfo info;
	info.on_draw = [](Window &window) {
		if (key_down(Key_f5)) {
			Profiler::reset();
			Profiler::enabled = true;
		}
		defer {
			if (key_down(Key_f5)) {
				Profiler::enabled = false;
				write_entire_file(TL_FILE_STRING("frame.tmd"), Profiler::output_for_timed());
			}
		};

		timed_block("Window::on_draw"s);

		glClearColor(.02,.05,.1,0);
		glClear(GL_COLOR_BUFFER_BIT);
		glViewport(window.client_size);
		glScissor(window.client_size);

		Imgui::begin_frame();

		Imgui::begin();

#pragma pack(push, 0)
		struct Event {
			s64 start;
			s64 end;
			u32 thread_id;
			u16 name_size;
			utf8 name[];
		};
#pragma pack(pop)

		static Buffer events_buffer;

		struct EventToDraw {
			EventToDraw *parent;
			Span<utf8> name;
			s64 start; // nanoseconds
			s64 end;
			u32 depth;
			v3f color;
			s64 self_duration;
			forceinline s64 duration() const {
				return end - start;
			}
		};
		static std::unordered_map<u32, List<EventToDraw>> thread_id_to_events_to_draw;
		static s64 events_start = 0;
		static s64 events_end = 0;
		static s64 events_duration = 0;
		static FileTracker file_tracker = {};

		struct EventGroup {
			Span<utf8> name;
			s64 total_duration;
		};

		static List<EventGroup> event_groups;

		enum MenuId {
			Menu_null,
			Menu_open,
		};

		struct Menu {
			MenuId id;
			Span<utf8> name;
			List<Menu> submenus;
			bool opened;
		};
		static Menu menus[] = {
			{Menu_null, u8"File"s, {
				{Menu_open, u8"Open"s},
			}},
		};
		static Menu *opened_menu = 0;

		static f64 events_view_scale = 1000000;
		static f64 events_scroll_amount, events_target_scroll_amount;
		static s32 groups_scroll_amount, groups_target_scroll_amount;

		Imgui::Button button;
		button.text_alignment = Imgui::TextAlignment_center;
		button.font_size = 16;
		button.content_padding = 4;
		button.background_color = {.1,.2,.3,1.};
		s32 menu_height = 0;
		for (auto &menu : menus) {
			button.text = menu.name;
			auto state = Imgui::button(button);
			++button.id;
			if (state.flags & Imgui::ButtonState_hovered) {
				if (opened_menu) {
					opened_menu->opened = false;
					opened_menu = &menu;
					opened_menu->opened = true;
				}
			}
			if (state) {
				if (opened_menu && opened_menu != &menu) {
					opened_menu->opened = false;
				}
				menu.opened = !menu.opened;
				opened_menu = menu.opened ? &menu : 0;
			}
			s32 next_menu_x = button.rect.max.x;
			if (!menu_height)
				menu_height = button.rect.max.y;

			if (menu.opened) {
				for (auto &submenu : menu.submenus) {
					button.text = submenu.name;
					button.rect += v2s{0, button.rect.size().y};
					button.rect.max = button.rect.min;
					if (Imgui::button(button)) {
						switch (submenu.id) {
							case Menu_open: {
								auto paths = open_file_dialog(FileDialog_file, {u8"tmd"s});
								if (paths.size) {
									auto path = paths[0];

									reset(file_tracker, path, [&] (FileTracker &tracker) {
										free(events_buffer);
										events_buffer = read_entire_file(tracker.path);
										if (events_buffer.data) {
											for (auto &[thread_id, events_to_draw] : thread_id_to_events_to_draw) {
												free(events_to_draw);
											}
											thread_id_to_events_to_draw.clear();
											event_groups.clear();

											events_start = max_value<s64>;
											events_end = min_value<s64>;

											auto event = (Event *)events_buffer.data;
											while (event) {
												EventToDraw d = {};
												d.name = {event->name, event->name_size};
												d.start = event->start;
												d.end = event->end;
												d.color = hsv_to_rgb(random_f32(get_hash(d.name)), 0.75f, 1);
												d.self_duration = d.duration();
												thread_id_to_events_to_draw[event->thread_id].add(d);

												events_start = min(events_start, d.start);
												events_end = max(events_end, d.end);

												event = (Event *)((u8 *)event->name + event->name_size);
												if ((u8 *)event > events_buffer.end() - sizeof(Event) || ((u8 *)event->name + event->name_size) > events_buffer.end()) {
													break;
												}
											}

											events_duration = events_end - events_start;

											events_view_scale = (f32)events_duration / window.client_size.x;
											events_scroll_amount = events_target_scroll_amount = 0;

											for (auto &[thread_id, events_to_draw] : thread_id_to_events_to_draw) {
												std::sort(events_to_draw.begin(), events_to_draw.end(), [](EventToDraw const &a, EventToDraw const &b) {
													if (a.start == b.start) {
														return a.duration() > b.duration();
													}
													return a.start < b.start;
												});

												List<EventToDraw *> parent_events;
												parent_events.allocator = temporary_allocator;

												for (auto &event : events_to_draw) {
													//
													// Insert group
													//
													auto found_group = find_if(event_groups, [&](auto &g) {return g.name == event.name;});
													auto &group = *(found_group ? found_group : &event_groups.add({event.name}));
													group.total_duration += event.duration();


													//
													// Calculate depth
													//
													event.start -= events_start;
													event.end -= events_start;

													EventToDraw *parent = 0;

												check_next_parent:
													if (parent_events.size) {
														parent = parent_events.back();
													}
													if (parent) {
														if (intersects(aabb_min_max(parent->start, parent->end), aabb_min_max(event.start, event.end))) {
															if (event.start == parent->start && event.duration() > parent->duration()) {
																// 'parent' is actually a child (deeper in the stack), and 'event' is parent,
																// so swap em
																if (parent->parent) {
																	parent->parent->self_duration =
																		parent->parent->self_duration
																		+ parent->duration()
																		- event.duration();
																}
																event.self_duration -= parent->duration();

																event.parent = parent->parent;
																event.depth  = parent->depth;
																parent->parent = &event;
																parent->depth += 1;
																parent_events.back() = &event;
																parent_events.add(parent);
															} else {
																event.parent = parent;
																event.depth = parent->depth + 1;
																parent_events.add(&event);
																parent->self_duration -= event.duration();
															}
															continue;
														} else {
															parent_events.pop();
															parent = 0;
															goto check_next_parent;
														}
													} else {
														parent_events.add(&event);
													}
												}
											}

											std::sort(event_groups.begin(), event_groups.end(), [](EventGroup const &a, EventGroup const &b) {
												return a.total_duration > b.total_duration;
											});
										}
									});
								}
								break;
							}
						}
					}
					++button.id;
				}
			}

			button.rect.min = {next_menu_x, 0};
			button.rect.max = button.rect.min;
		}

		if (mouse_up(0) && opened_menu && !Imgui::hovering_interactive_element) {
			opened_menu->opened = false;
			opened_menu = 0;
		}

		Imgui::end();

		Imgui::begin();

		static Imgui::Split split = {
			.split_t = 0.25f,
			.color = {.2,.3,.4,1.},
		};
		split.rect = to_zero(Imgui::current_region.rect);
		Imgui::split(split);

		s32 const side_panel_width = 256;
		{
			Imgui::begin_region(split.half[0]);
			auto [sort_rect, content_rect] = Imgui::split(24, Imgui::Dock_top).data;
			{
				Imgui::begin_region(sort_rect);

				// TODO: group sorting

				Imgui::end_region();
			}

			{
				Imgui::begin_region(content_rect);

				s32 button_height = 24;
				s32 start_y = 0;

				button.content_padding = 0;
				button.font_size = 12;
				button.text_alignment = Imgui::TextAlignment_top_left;
				for (auto &group : event_groups) {
					button.rect = aabb_min_max(v2s{0, groups_scroll_amount+start_y}, {Imgui::current_region.rect.size().x-16, groups_scroll_amount+start_y + button_height});
					button.text = tformat(u8"% - %", nanoseconds(group.total_duration), group.name);
					Imgui::button(button);
					++button.id;
					if (Imgui::should_set_tooltip) Imgui::set_tooltip(button.text);
					start_y += button_height;
				}

				Imgui::ScrollBar<s32> scroll_bar;
				scroll_bar.rect = Imgui::get_dock(16, Imgui::Dock_right);
				scroll_bar.scroll_amount = &groups_scroll_amount;
				scroll_bar.target_scroll_amount = &groups_target_scroll_amount;
				scroll_bar.total_size = start_y;
				Imgui::scroll_bar(scroll_bar);
				Imgui::end_region();
			}

			Imgui::end_region();
		}
		{

			Imgui::begin_region(split.half[1]);
			defer { Imgui::end_region(); };

			auto event = (Event *)events_buffer.data;
			s32 button_height = 24;

			s32 start_y = button_height;
			s32 max_depth = 0;

			if (key_down('X')) {
				events_view_scale = 1000000;
			}
			if (key_down('R')) {
				events_scroll_amount = 0;
				events_target_scroll_amount = 0;
				events_view_scale = (f64)events_duration / window.client_size.x;
			}

			if (!thread_id_to_events_to_draw.empty()) {
				s64 notch_scale = pow(10, ::floor(log10(events_view_scale) + 6));
				auto notch_time = "ns"s;
				if (notch_scale >= 1000) { notch_scale /= 1000; notch_time = "ns"s; }
				if (notch_scale >= 1000) { notch_scale /= 1000; notch_time = "us"s; }
				if (notch_scale >= 1000) { notch_scale /= 1000; notch_time = "ms"s; }
				if (notch_scale >= 1000) { notch_scale /= 1000; notch_time = "s"s; }

				auto draw_marks = [&](f64 scale, f32 alpha, bool draw_text) {
					auto x_to_mark_index = [&](s32 x) {
						return ((x - (s64)events_scroll_amount) * events_view_scale) / pow(10, ::floor(log10(events_view_scale * scale)));
					};
					for (s64 mark_index = x_to_mark_index(0); mark_index < x_to_mark_index(Imgui::current_region.rect.size().x); ++mark_index) {
						aabb<v2s> rect;
						s64 t = mark_index * pow(10, ::floor(log10(events_view_scale * scale)));

						//auto mark_index_to_x = [&](s32 mark_index) {
						//	return mark_index * pow(10, floor(log10(view_scale * scale))) / view_scale + scroll_amount;
						//};

						rect.min.x = t / events_view_scale + events_scroll_amount;
						rect.min.y = 0;
						rect.max.x = rect.min.x + 1;
						rect.max.y = Imgui::current_region.rect.size().y;
						Imgui::panel(rect, v4f{1,1,1,alpha});

						if (draw_text) {
							Imgui::label(32173812 + mark_index, tformat(u8"% %"s, notch_scale * mark_index, notch_time), aabb_min_size(rect.min, {1000,1000}), Imgui::TextAlignment_top_left, 16);
						}
					}
				};
				draw_marks(100, .1, false);
				draw_marks(1000, .2, true);
			}

			button.content_padding = 0;
			button.background_color.w = 0.5f;
			for (auto &[thread_id, events_to_draw] : thread_id_to_events_to_draw) {
				button.text = tformat(u8"Thread %", thread_id);
				button.rect = aabb_min_max(v2s{0, start_y}, v2s{(s32)window.client_size.x,start_y + button_height});
				button.text_alignment = Imgui::TextAlignment_top_left;
				button.font_size = 12;
				button.background_color.xyz = hsv_to_rgb(random_f32(thread_id), 0.75f, 1);
				button.align_text_to_visible_rect = false;
				Imgui::button(button);
				++button.id;
				start_y += button_height;
				button.align_text_to_visible_rect = true;
				for (auto &event : events_to_draw) {
					button.rect.min.x = event.start / events_view_scale + events_scroll_amount;
					button.rect.max.x = max(event.end   / events_view_scale + events_scroll_amount, button.rect.min.x + 1);
					button.rect.min.y = start_y + button_height * (event.depth + 0);
					button.rect.max.y = start_y + button_height * (event.depth + 1);
					button.text_alignment = Imgui::TextAlignment_center;
					button.font_size = 12;
					button.background_color.xyz = event.color;
					if (button.rect.size().x > 1) {
						button.text = event.name;
					} else {
						button.text = {};
					}
					Imgui::button(button);
					++button.id;
					if (Imgui::should_set_tooltip) {
						Imgui::set_tooltip(tformat(u8"% (% total, % self)", event.name, nanoseconds(event.end - event.start), nanoseconds(event.self_duration)));
					}
					max_depth = max(max_depth, event.depth);
				}
				start_y += (max_depth + 1) * button_height;
			}

			Imgui::ScrollBar<f64> scroll_bar;
			scroll_bar.rect = Imgui::get_dock(16, Imgui::Dock_bottom);
			scroll_bar.scroll_amount = &events_scroll_amount;
			scroll_bar.target_scroll_amount = &events_target_scroll_amount;
			scroll_bar.total_size = events_duration;
			scroll_bar.scale = &events_view_scale;
			scroll_bar.min_scale = 0.01f;
			scroll_bar.max_scale = 1000000000;
			scroll_bar.flags =
				Imgui::ScrollBar_zoom_with_wheel |
				Imgui::ScrollBar_pan_with_mouse |
				Imgui::ScrollBar_no_clamp
			;
			Imgui::scroll_bar(scroll_bar);
		}

		Imgui::end();

		Imgui::end_frame();

		present();

		update(file_tracker);

		if (Imgui::updated_text_count)
			print("Updated % text vertex buffers\n", Imgui::updated_text_count);

		clear_temporary_storage();
	};
	info.on_size = [](Window &window) {
	};
	info.title = u8"Timed"s;
	info.min_client_size = {1280, 720};
	create_window(&window, info);

	init_opengl(window->handle, true);

	init_opengl_shader_catalog(shader_catalog, TL_FILE_STRING("../data/shaders"s));

	Imgui::init(window, font_collection);
	wglSwapIntervalEXT(1);

	Profiler::enabled = false;

	while (update(window)) {
		update(shader_catalog);
	}

	return 0;
}
